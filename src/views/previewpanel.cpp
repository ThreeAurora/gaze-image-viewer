#include "previewpanel.h"
#include "livephoto.h"
#include "thumbnailer.h"
#include "logger.h"
#include "wicdecode.h"
#include "settings.h"
#include "labelstore.h"
#include "markdown.h"
#include "pdfrender.h"
#include "textlimit.h"
#include "imgproc.h"
#include "constants.h"
#include "viewerhotkeys.h"
#include "shelldelete.h"
#include "fileentry.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QtMath>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QSplitter>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QMimeData>
#include <QMediaDevices>
#include <QWidgetAction>
#include <QThreadPool>
#include <QTextEdit>
#include <QFile>
#include <QMetaObject>
#include <QClipboard>
#include <QBrush>
#include <QScrollBar>
#include <cmath>
#include <QStyle>
#include <QWidgetAction>
#include <QMenu>
#include <QVideoFrame>
#include <QVideoSink>
#include "previewpanel_internal.h"

// (VIDFRAME 帧旁听探针已于 2026-09-01 剪除:它完成了 #101 AV1 黑屏、#104 闪回、
//  #121 HDR 三轮诊断 —— 结论都写进了 TODO_ALL。播放侧的常驻通道留 hb/mediaStatus。)

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    // 背景由 paintEvent 自绘(样式表背景画不出挡板底纹的平铺图案)
    setMouseTracking(true);   // 悬停也要收移动事件:全屏隐藏指针后靠它恢复

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 空态占位(未选中任何文件时;样式在应用级 QSS,#89 收敛)
    m_placeholder = new QLabel;
    m_placeholder->setObjectName("pvPlaceholder");
    m_placeholder->setAlignment(Qt::AlignCenter);
    layout->addWidget(m_placeholder, 1);

    // 图片标签:不进布局——缩放/拖动需要自由定位,
    // 尺寸可超面板(超出部分裁剪,拖动=移动视口),否则放大后只剩"片段"
    m_imgLabel = new QLabel(this);
    m_imgLabel->setAlignment(Qt::AlignCenter);
    // #208:必须开跟踪。全屏画面 95% 面积被它盖住,没跟踪 QLabel 就收不到
    // 悬停 MouseMove、也不会冒泡 → 面板的 move 处理(显隐信息条/恢复光标)
    // 在死区里根本不执行:信息条移开不消失、隐藏后的光标永不恢复
    m_imgLabel->setMouseTracking(true);
    m_imgLabel->hide();

    // 音频标签
    m_audioLabel = new QLabel;
    m_audioLabel->setObjectName("pvAudio");   // 样式在应用级 QSS(#89 收敛)
    m_audioLabel->setAlignment(Qt::AlignCenter);
    m_audioLabel->hide();
    layout->addWidget(m_audioLabel, 1);

    // 音频波形画布:解码+聚合在专属线程(audiowave.h),这里只收快照画像素。
    // stretch 3:波形吃音频区大头,文件名条占 1/4
    m_waveLabel = new QLabel;
    m_waveLabel->setObjectName("pvWave");   // 样式在应用级 QSS(#89 收敛)
    m_waveLabel->setAlignment(Qt::AlignCenter);
    m_waveLabel->hide();
    layout->addWidget(m_waveLabel, 3);

    // RAW 占位(#140):说明行居中 + 加载按钮**右上角**(2026-09-02 用户令)。
    // 内嵌 JPEG 预览缺席/想全解时点它;有内嵌预览时 rawBox 直接隐藏走图片形态。
    m_rawBox = new QWidget;
    m_rawBox->hide();
    auto* rawL = new QVBoxLayout(m_rawBox);
    rawL->setContentsMargins(0, 0, 0, 0);
    rawL->setSpacing(12);
    // 第一行:按钮右对齐(右上角),随容器缩放保持贴边
    auto* rawBtnRow = new QHBoxLayout;
    rawBtnRow->addStretch(1);
    // RAW 按钮/说明样式在应用级 QSS(QPushButton#pvRawBtn 等,#89 收敛)
    m_rawBtn = new QPushButton;
    m_rawBtn->setObjectName("pvRawBtn");
    m_rawBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rawBtn, &QPushButton::clicked, this, [this]() { decodeRawAsync(); });
    rawBtnRow->addWidget(m_rawBtn);
    rawL->addLayout(rawBtnRow);
    rawL->addStretch(1);
    m_rawCaption = new QLabel;
    m_rawCaption->setObjectName("pvRawCaption");
    m_rawCaption->setAlignment(Qt::AlignCenter);
    rawL->addWidget(m_rawCaption);
    rawL->addStretch(1);
    layout->addWidget(m_rawBox, 1);

    // 悬浮「加载原始RAW」(#140b):不进布局,盖在预览右上角。内嵌预览加载完
    // rawBox 整体藏掉,全解入口靠它保留 —— 用户令"加载完内嵌图之后按钮加回来"
    m_rawFullBtn = new QPushButton(gazeTr("加载原始 RAW"), this);
    m_rawFullBtn->setObjectName("pvRawBtn");
    m_rawFullBtn->setCursor(Qt::PointingHandCursor);
    connect(m_rawFullBtn, &QPushButton::clicked, this, [this]() { decodeRawAsync(); });
    m_rawFullBtn->hide();

    // 悬浮 CMYK 切换钮(#243):不进布局,盖在预览右上角。CMYK 印刷 JPG 未内嵌
    // 色彩配置时两套解码器颜色解释不同:默认 WIC 印刷口径(与缩略图/打印/系统
    // 照片一致);勾选=Qt 数值直接反演(通常偏亮),仅供对比,切文件自动复位。
    m_cmykBtn = new QPushButton(QStringLiteral("CMYK"), this);
    m_cmykBtn->setObjectName("pvCmykBtn");   // 样式在应用级 QSS(#89 收敛)
    m_cmykBtn->setCursor(Qt::PointingHandCursor);
    m_cmykBtn->setCheckable(true);
    m_cmykBtn->setToolTip(gazeTr(
        "CMYK 印刷图（未内嵌色彩配置）:默认按印刷标准转换,与缩略图/打印/系统照片一致。\n"
        "勾选=数值直接反演(通常偏亮),仅供对比,切文件自动复位。"));
    connect(m_cmykBtn, &QPushButton::clicked, this, [this]() {
        m_cmykAlt = m_cmykBtn->isChecked();
        if (m_mode != "image" || m_filePath.isEmpty()) return;
        ++m_imgReqGen;                  // 走与切文件同一套异步解码:旧画面保持,就绪即替换
        if (!m_fullBusy) decodeFullAsync(m_filePath, m_imgReqGen);
        // fullBusy:在飞任务完成后 onFullDecoded 自动补发最新代号(带新口径)
    });
    m_cmykBtn->hide();

    // txt 文本预览(等宽字体,只读;样式在应用级 QSS QTextEdit#pvText,#89 收敛)
    m_textEdit = new QTextEdit;
    m_textEdit->setObjectName("pvText");
    m_textEdit->setReadOnly(true);
    m_textEdit->hide();
    layout->addWidget(m_textEdit, 1);

    // 视频区
    m_videoWidget = new QWidget;
    m_videoWidget->setObjectName("pvVideo");   // 底色在应用级 QSS(#89 收敛)
    m_videoWidget->setMouseTracking(true);   // #208 同图片标签:全屏悬停事件死区
    m_videoWidget->hide();
    layout->addWidget(m_videoWidget, 1);

    // 图片/GIF 形态的空间吸收器:布局里带 stretch 的四项(占位/音频/文本/视频区)
    // 在图片形态下全是隐藏的,剩下的 40px 控制栏会被 QVBoxLayout 居中
    // (剩余空间均分到上下)。这个不画东西、不收鼠标的吸收器顶到 stretch,
    // 把控制栏压回底部;只由 applyGifChrome 点亮,其余形态一律隐藏。
    m_imgSpace = new QWidget;
    m_imgSpace->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_imgSpace->hide();
    layout->addWidget(m_imgSpace, 1);

    // 控制栏(XnView 排布):上一文件 / 播放暂停 / 停止 / 音量 / 进度 / 时间
    // 底色/上边线在应用级 QSS(QWidget#pvControlBar 后代规则,#89 收敛)
    m_controlBar = new QWidget;
    m_controlBar->setObjectName("pvControlBar");
    m_controlBar->setFixedHeight(40);
    m_controlBar->hide();

    auto* cl = new QHBoxLayout(m_controlBar);
    cl->setContentsMargins(10, 4, 10, 4);
    cl->setSpacing(4);

    // 按钮统一样式在应用级 QSS(QWidget#pvControlBar QPushButton/QToolButton,
    // #89 收敛) + 主题色图标:播放/暂停图标在别处切换时必须同样走 themeIcon,
    // 否则标准图标自带深色,在深底上直接"变黑看不见";浅色主题下白图标又会
    // 在浅灰栏上隐身,所以染的是"随主题反转"的文字色(深色白/浅色黑)
    auto mkBtn = [&](auto* b, QStyle::StandardPixmap sp, int w, const QString& tip) {
        b->setIcon(pp_impl::themeIcon(style()->standardIcon(sp)));
        b->setIconSize(QSize(16, 16));
        b->setFixedSize(w, 28);
        b->setToolTip(tip);
        cl->addWidget(b);
    };

    m_btnPrev = new QPushButton;
    mkBtn(m_btnPrev, QStyle::SP_MediaSkipBackward, 30,
          gazeTr("上一个文件")); // 上一个文件
    connect(m_btnPrev, &QPushButton::clicked, this, [this]() { emit navFile(-1); });

    // 播放/暂停:m_player 会被 teardownPlayer 销毁并重建,
    // 若把 connect 放在 setupPlayer 里,每轮循环都会加一份,
    // 一次点击 = N 次状态切换。故连接固定在构造函数建立,槽内以 m_player 判空。
    m_btnPlay = new QPushButton;
    mkBtn(m_btnPlay, QStyle::SP_MediaPlay, 32,
          gazeTr("播放/暂停")); // 播放/暂停
    connect(m_btnPlay, &QPushButton::clicked, this, [this]() {
        // #97:GIF 与视频共用这一条控制栏,必须经 togglePlayPause 分流,
        // 否则在 GIF 上点播放会把上一段视频接着放出来
        togglePlayPause();
    });

    m_btnStop = new QPushButton;
    mkBtn(m_btnStop, QStyle::SP_MediaStop, 30,
          gazeTr("停止(回到开头, T)")); // 停止(回到开头, T)
    connect(m_btnStop, &QPushButton::clicked, this, [this]() {
        if (m_isGif) {
            setGifPaused(true);   // 先停:跳帧走暂停态,避开运行态 jumpToFrame 卡死
            gifSeekMs(0);
            return;
        }
        if (!m_player) return;
        m_player->stop();          // Qt6 stop 同时把位置归零 → 再播从头开始
        m_progress->setValue(0);
    });

    m_btnVolume = new QToolButton;
    mkBtn(m_btnVolume, QStyle::SP_MediaVolume, 32,
          gazeTr("音量"));   // 音量

    m_progress = new QSlider(Qt::Horizontal);
    m_progress->setObjectName("pvProgress");   // 滑槽/旋钮样式在应用级 QSS(#89 收敛)
    m_progress->setRange(0, 0);
    m_progress->setFixedHeight(16);
    m_progress->setMouseTracking(true);   // hover 移动即请求秒级缩略图
    m_progress->installEventFilter(this);   // 播放条点击直接跳转
    // 同 m_btnPlay,拖动进度条的槽也移出 setupPlayer 以免重复注册。
    connect(m_progress, &QSlider::sliderMoved, this, [this](int pos) {
        if (m_isGif) { gifSeekMs(pos); return; }
        if (m_player) m_player->setPosition(pos);
    });
    cl->addWidget(m_progress, 1);
    // 滑条与时间文本恒定 8px(4 布局间距 + 4):旧版时间标签 setFixedWidth(120)
    // 右对齐,短串左边 ~60px 空白全摊在滑条和时间之间;去掉固定宽后标签随
    // 文本自适应(长时长如 1000:10:23 变宽时滑条自动让位,间距不变)
    cl->addSpacing(4);

    m_timeLabel = new QLabel("0:00 / 0:00");
    m_timeLabel->setObjectName("pvTime");   // 样式在应用级 QSS(#89 收敛)
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timeLabel->setToolTip(gazeTr("点击切换 已播/剩余时间"));
    m_timeLabel->installEventFilter(this);  // 点击切换剩余时间显示
    cl->addWidget(m_timeLabel);

    layout->addWidget(m_controlBar);

    // LIVE 徽章(动态照片播放时的右上角标识,child of videoWidget;样式在应用级 QSS)
    m_liveBadge = new QLabel("LIVE", m_videoWidget);
    m_liveBadge->setObjectName("pvLiveBadge");
    m_liveBadge->adjustSize();
    m_liveBadge->hide();

    // GIF 时钟(#94):两条都是成员单发定时器。播放拍用 start() 取代
    // QTimer::singleShot —— 后者每排一拍就多一个未决事件,拖动期间
    // gifSeekMs 会反复重排,未决拍堆成"快进+忽快忽慢";start() 天然取消上一拍。
    // 擦洗拍 0ms:把同一事件循环批次里的多个 move 并成"最后一个目标帧"再解。
    m_gifPlayTimer = new QTimer(this);
    m_gifPlayTimer->setSingleShot(true);
    connect(m_gifPlayTimer, &QTimer::timeout, this, &PreviewPanel::gifPlayTick);
    m_gifSeekTimer = new QTimer(this);
    m_gifSeekTimer->setSingleShot(true);
    connect(m_gifSeekTimer, &QTimer::timeout, this, &PreviewPanel::gifApplySeek);

    // 事件过滤器
    m_imgLabel->installEventFilter(this);
    m_videoWidget->installEventFilter(this);   // 视频区左键=播放/暂停(见 eventFilter)
    // m_waveLabel 的过滤器必须在这里(构造完成)挂:若在波形块里挂,addWidget 的
    // reparent 事件会同步进 eventFilter,彼时 m_textEdit 还没建 → 空指针崩溃
    m_waveLabel->installEventFilter(this);   // Resize → renderWave 重画
    m_textEdit->viewport()->installEventFilter(this);  // #115:滚轮落在 QTextEdit 的 viewport 上(见 eventFilter)
    installEventFilter(this);

    // 音量按钮:弹出竖向滑条 + 0-100 数值(拖动实时刷新,按钮 tooltip 跟着走)
    connect(m_btnVolume, &QToolButton::clicked, this, [this]() {
        if (!m_audioOutput) return;
        QMenu volMenu(this);
        auto* wrap = new QWidget(&volMenu);
        auto* wl = new QHBoxLayout(wrap);
        wl->setContentsMargins(10, 10, 10, 10);
        wl->setSpacing(6);
        auto* slider = new QSlider(Qt::Vertical, wrap);
        slider->setRange(0, 100);
        slider->setValue(static_cast<int>(m_audioOutput->volume() * 100));
        slider->setFixedSize(24, 110);
        auto* val = new QLabel(QString::number(slider->value()), wrap);
        val->setObjectName("pvVolVal");   // 样式在应用级 QSS(#89 收敛)
        val->setFixedWidth(30);
        val->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        wl->addWidget(slider);
        wl->addWidget(val);
        connect(slider, &QSlider::valueChanged, this, [this, val](int v) {
            if (m_audioOutput) m_audioOutput->setVolume(v / 100.0);
            val->setText(QString::number(v));
            m_btnVolume->setToolTip(
                gazeTr("音量 %1").arg(v));
        });
        auto* act = new QWidgetAction(&volMenu);
        act->setDefaultWidget(wrap);
        volMenu.addAction(act);
        volMenu.exec(m_btnVolume->mapToGlobal(
            QPoint(m_btnVolume->width() / 2 - 60, -140)));
    });

    // ── Viewer|Fullscreen/showScrollbar:图大于视口时的覆盖式滚动条 ──
    // 走覆盖而非布局:图片标签是自由定位的,塞进布局会破坏"放大后仍可拖动查看"的模型
    auto mkScroll = [this](Qt::Orientation o) {
        auto* sb = new QScrollBar(o, this);
        sb->setObjectName("pvOverlaySb");   // 覆盖滚动条样式在应用级 QSS(#89 收敛)
        sb->hide();
        return sb;
    };
    m_hScroll = mkScroll(Qt::Horizontal);
    m_vScroll = mkScroll(Qt::Vertical);
    auto scrollTo = [this](int) {
        if (m_mode != "image") return;
        m_imgLabel->move(clampedLabelPos(
            QPoint(m_hScroll->isVisible() ? -m_hScroll->value() : m_imgLabel->x(),
                   m_vScroll->isVisible() ? -m_vScroll->value() : m_imgLabel->y())));
    };
    connect(m_hScroll, &QScrollBar::valueChanged, this, scrollTo);
    connect(m_vScroll, &QScrollBar::valueChanged, this, scrollTo);

    // ── Fullscreen/showToolbar:全屏浮动工具条(上一/下一/适应/1:1/退出) ──
    // (#230:旧左上角信息条 m_infoLabel 整链删除 —— 文件名/大小信息由 G 全屏
    // 胶片条题注承担,"全屏只留画面"不再有第二处文字)
    m_floatBar = new QWidget(this);
    m_floatBar->setObjectName("pvFloatBar");   // 底/边框样式在应用级 QSS(#89 收敛)
    m_floatBar->hide();
    {
        auto* fl = new QHBoxLayout(m_floatBar);
        fl->setContentsMargins(6, 4, 6, 4);
        fl->setSpacing(4);
        // 按钮样式在应用级 QSS(QWidget#pvFloatBar QPushButton,#89 收敛)
        auto add = [&](QStyle::StandardPixmap sp, const QString& tip, auto&& fn) {
            auto* b = new QPushButton;
            b->setIcon(pp_impl::whiteIcon(style()->standardIcon(sp)));
            b->setToolTip(tip);
            QObject::connect(b, &QPushButton::clicked, this, fn);
            fl->addWidget(b);
        };
        add(QStyle::SP_MediaSkipBackward, gazeTr("上一个文件"),
            [this]() { emit navFile(-1); });
        add(QStyle::SP_MediaSkipForward, gazeTr("下一个文件"),
            [this]() { emit navFile(1); });
        add(QStyle::SP_DialogResetButton, gazeTr("适应窗口"),
            [this]() { fitAuto(); });
        add(QStyle::SP_DialogCloseButton, gazeTr("退出全屏"),
            [this]() { if (inFullscreen()) window()->showNormal(); });
    }

    // ── Viewer/panTool:右下角平移导航小窗(图溢出视口时才出现) ──
    m_panTool = new QWidget(this);
    m_panTool->setObjectName(QStringLiteral("panNavTool"));
    m_panTool->setFixedSize(150, 110);
    // 底/边框样式在应用级 QSS(QWidget#panNavTool,#89 收敛)。选择器必须带
    // #objectName:写成裸 QWidget{} 会**连带子控件**一起吃到这条
    // 边框与底色(#111 的账)——m_panThumb 一旦有了 1px 边框,Qt 就把缩略图 pixmap
    // 画进内容矩形(整体右移 1px),而 ox/oy 按控件全宽算,蓝框就比图偏左偏上一格
    m_panTool->hide();
    m_panThumb = new QLabel(m_panTool);
    m_panThumb->setAlignment(Qt::AlignCenter);
    m_panThumb->setGeometry(1, 1, 148, 108);
    m_panView = new QWidget(m_panTool);
    m_panView->setObjectName("pvPanView");   // 蓝框样式在应用级 QSS(#89 收敛)
    m_panView->hide();
    // 拖动蓝框/缩略图 → 视口跟随(事件过滤器在 eventFilter 里处理)
    m_panThumb->setCursor(Qt::PointingHandCursor);
    m_panThumb->installEventFilter(this);
    m_panView->installEventFilter(this);

    // ── #82 PDF 页导航条(仅预览 PDF 时出现,贴底居中)──
    // 底/边框样式在应用级 QSS(QWidget#pvPdfBar 后代规则,#89 收敛)
    m_pdfBar = new QWidget(this);
    m_pdfBar->setObjectName("pvPdfBar");
    m_pdfBar->hide();
    {
        auto* pl = new QHBoxLayout(m_pdfBar);
        pl->setContentsMargins(8, 5, 8, 5);
        pl->setSpacing(6);
        // 按钮样式在应用级 QSS(QWidget#pvPdfBar QPushButton,#89 收敛)
        m_pdfPrev = new QPushButton(gazeTr("◀ 上一页"));
        m_pdfNext = new QPushButton(gazeTr("下一页 ▶"));
        m_pdfLabel = new QLabel(gazeTr("第 1 页"));
        m_pdfLabel->setObjectName("pvPdfLabel");
        connect(m_pdfPrev, &QPushButton::clicked, this,
                [this]() { pdfGotoPage(m_pdfPage - 1); });
        connect(m_pdfNext, &QPushButton::clicked, this,
                [this]() { pdfGotoPage(m_pdfPage + 1); });
        pl->addWidget(m_pdfPrev);
        pl->addWidget(m_pdfLabel);
        pl->addWidget(m_pdfNext);
    }

    // 设置页改动 → 背景/挡板/图片边框即时重涂(无需重启)
    applyBackdrop();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        applyBackdrop();
        applyViewerChrome();
        if (m_mode == "image") render();
        if (!pp_impl::s_bool("Fullscreen/hideCursor", true)) restoreCursor();
    });

    // Fullscreen/hideCursor:全屏时指针静止 3s 后隐藏,移动即恢复
    m_cursorTimer.setSingleShot(true);
    m_cursorTimer.setInterval(3000);
    connect(&m_cursorTimer, &QTimer::timeout, this, [this]() {
        if (inFullscreen() && pp_impl::s_bool("Fullscreen/hideCursor", true) && !m_dragging) {
            m_cursorHidden = true;
            setCursor(Qt::BlankCursor);
            if (m_imgLabel->underMouse()) m_imgLabel->setCursor(Qt::BlankCursor);
        }
    });

    // 媒体心跳:视频/音频播放期间每 5s 报一次状态(卡死时看最后一条 hb 定位)
    auto* mediaHb = new QTimer(this);
    connect(mediaHb, &QTimer::timeout, this, [this]() {
        if (!m_player || (m_mode != "video" && m_mode != "audio")) return;
        Logger::event(QStringLiteral("hb state=%1 pos=%2 dur=%3 src='%4'")
                          .arg(int(m_player->playbackState()))
                          .arg(m_player->position()).arg(m_player->duration())
                          .arg(m_player->source().toLocalFile()));
    });
    mediaHb->start(5000);
}

PreviewPanel::~PreviewPanel() {
    if (m_remuxProc) {   // 重封装在途就收掉,不让 ffmpeg 孤儿跑完
        m_remuxProc->kill();
        m_remuxProc->waitForFinished(500);
    }
    teardownWave();   // 波形线程必须走完再拆面板(wait 2s 兜底)
    teardownPlayer();
    cleanupExtractCache();
}

void PreviewPanel::setViewerMode(bool on) {
    if (m_viewerMode == on) return;
    m_viewerMode = on;
    // 那张表过去收不到键，原因是全项目没有一处向本面板要过焦点 —— 不是 NoFocus
    // "屏蔽"了焦点：实测(cache/tmp/focus_probe.cpp)显式 setFocus() 无视策略，
    // 照样当上 focusWidget 并接到按键。所以真正生效的是下面那句 setFocus()。
    // 策略切换要留着：它管的是"点一下/敲 Tab 能不能落到面板"。查看器里网格窗格
    // 已被 setVisible(false)，浏览器那套键盘通路不在，必须让面板可点可 Tab；
    // 退回浏览器时还回 NoFocus，否则点一下预览区就抢走网格的方向键。
    setFocusPolicy(on ? Qt::StrongFocus : Qt::NoFocus);
    if (on) setFocus();
    applyBackdrop();
    applyViewerChrome();
}

void PreviewPanel::loadFile(const QString& path) {
    // 换文件(或清空):递增代号,作废任何在途的后台解码结果
    ++m_imgReqGen;
    m_rawFromImage = false;         // 新装载:上一文件的"从图片发起全解"标记作废
    if (m_rawFullBtn) m_rawFullBtn->hide();
    if (m_cmykBtn) m_cmykBtn->hide();   // #243:CMYK 对比口径只属于"正在看的那一个文件"
    m_cmykAlt = false;
    // 波形跟文件走:换文件即停旧解码(选中图片时上一音频不该在后台白烧 CPU;
    // 若这次又落在音频上,showAudio 的 start 会带新代次重新起解)
    if (m_waveWorker)
        QMetaObject::invokeMethod(m_waveWorker, "cancel");
    m_liveInfo.reset();
    // 同路径重复 loadFile(启动恢复双触发)不清票:票对应的就是这次装载,
    // 清掉会让 showVideo 走"装载期抢接输出+play"旧路,实测掐断视频管线(只出声不出画)
    if (m_pendingPlay != path) m_pendingPlay.clear();
    Logger::event(QStringLiteral("loadFile '%1'").arg(path));
    if (path.isEmpty()) { clear(); return; }
    QFileInfo fi(path);
    if (!fi.exists()) { Logger::event("loadFile: not exist"); clear(); return; }

    // 换文件(区别于窗口 resize 触发的 fitAuto):Viewer/resetAutoOnNav 靠它判断
    m_navigating = true;
    m_filePath = path;
    m_livePhotoOriginalPath.clear();
    m_isLivePhoto = false;
    stopMovie();   // 任何类型切换都先回收 GIF 动画(防泄漏/防隐藏继续解码)

    // 目录:预览框留空。内容 2x2 拼贴表达的是"这个文件夹长什么样",
    // 属缩略图卡片的职责(Thumbs/folder4 已在那里出图);预览框只对
    // "选中的那一个内容"负责,文件夹没有这样一个内容。
    if (fi.isDir()) { showNoPreview(); return; }

    // 不再在此处清屏:图片→图片切换保持上一张画面直到新图就绪(无缝,无黑帧);
    // 视频/音频/GIF/未知 分支各自隐藏图片标签(视频黑底过渡属预期)
    QString ext = fi.suffix().toLower();

    // 注:不再 teardownPlayer —— QMediaPlayer/QVideoWidget 永久复用,
    // 任何"销毁重建"路径都会出现无帧透明窗口期(切换瞬间闪回旧画面)

    // ── Live Photo 检测（在扩展名路由之前）──
    // 只做标记 + 后台预提取,不自动播放——播放只由"单击预览窗格"触发
    if (IMAGE_EXTS.count("." + ext)) {
        auto liveInfo = LivePhoto::detect(path);
        if (liveInfo) {
            Logger::event(QStringLiteral("live detect: type=%1 embedded=%2 video='%3'")
                              .arg(liveInfo->type).arg(liveInfo->embedded)
                              .arg(liveInfo->videoPath));
            m_liveInfo = liveInfo;   // 单击预览窗格靠它找到视频
            // 后台预提取(不涉及媒体后端,纯 remux):点预览窗格时即点即播
            if (liveInfo->embedded && liveInfo->videoOffset >= 0
                && !m_extractCache.contains(path)) {
                startExtractAsync(path, *liveInfo);
            }
        }
    }

    if (IMAGE_EXTS.count("." + ext)) {
        showImage(path);
        // Viewer/autoPlayAudioCompanion:同名音频伴侣文件自动播放(默认关)
        playAudioCompanion(path);
    } else if (VIDEO_EXTS.count("." + ext)) {
        showVideo(path);
    } else if (AUDIO_EXTS.count("." + ext)) {
        showAudio(path);
    // 文本一族的预览默认全关(#111,用户 2026-08-31:「预览文本/PDF，单独加设置，
    // 默认不开，打勾是开」)。Markdown 也是文本,一并归到这条裁决下默认关。
    } else if (ext == "txt" && pp_impl::s_bool("Preview/previewTxt", false)) {
        showText(path);
    } else if (ext == "md") {
        // md 总要给个看的形态:开=渲染样式;关=纯文本(这样右键菜单里
        // "以 Markdown 样式展示"的开关才摸得着,不至于整个没有预览)
        if (pp_impl::s_bool("Preview/showMd", false)
            && pp_impl::s_bool("Preview/mdRenderStyle", true)) showMarkdown(path);
        else showText(path);
    } else if (ext == "pdf" && pp_impl::s_bool("Preview/showPdf", false)) {
        showPdf(path);
    } else if (RAW_EXTS.count("." + ext)) {
        // RAW(#140):不自动解码(全解数秒级重活),占位 + 按需全解按钮
        showRawPlaceholder(path);
    } else {
        showNoPreview();
    }
}

// 无预览出口:目录 / 未知类型共用。只藏视图,不 teardownPlayer
// (播放器实例永久复用,销毁重建会露出无帧的透明窗口期,切换瞬间闪回旧画面)
void PreviewPanel::showNoPreview() {
    m_mode = "none";
    if (m_player) m_player->stop();
    if (m_liveBadge) m_liveBadge->hide();
    m_placeholder->show();
    m_imgLabel->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_textEdit->hide();
    m_controlBar->hide();
    m_imgSpace->hide();
}

void PreviewPanel::setupPlayer() {
    if (m_player) return;

    // 首次创建实测可达数秒(FFmpeg 后端加载/硬解设备枚举/音频端点),埋探针:
    // 下次日志直接看到这笔开销落在谁头上,不用再靠 loadFile→showVideo 的时间差倒推
    QElapsedTimer initSw;
    initSw.start();

    // Qt6 API: QMediaPlayer + QAudioOutput
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8);
    Logger::event(QStringLiteral("setupPlayer: QMediaPlayer+QAudioOutput %1 ms")
                      .arg(initSw.elapsed()));

    // 注:视频控件(QVideoWidget)统一由 showVideo → ensureVideoWidget() 创建/复用,
    // 绝不在每次切换视频时重建 —— 新建控件存在"无帧透明窗口期",
    // 会透出下层残留画面(表现为切换视频瞬间闪回先前画面)
    if (m_mode == "video" && m_vw) {
        m_player->setVideoOutput(m_vw);
        m_videoOutAttached = true;
    }

    // 播放状态（视频/音频通用）
    connect(m_player, &QMediaPlayer::playbackStateChanged,
            this, [this](QMediaPlayer::PlaybackState state) {
        if (!m_player || !m_btnPlay) return;
        m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(
            state == QMediaPlayer::PlayingState
                ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay)));
        // #104:armed 期间(等待本路首帧)不得由 PlayingState 露出画面——
        // PlayingState 比首帧早到,此刻视频面还是上一段的末帧。
        // 未布防(布防失败/太早)时这里兜底,免得藏起来的 vw 没人放出来。
        if (state == QMediaPlayer::PlayingState && !m_coverArmed
            && (m_mode == "video" || m_isLivePhoto)) {
            revealVideo();
        }
    });

    // Live Photo: 视频播完 → 切回静态图(播放器保留复用)。
    // 连接无条件建立(首次 setupPlayer 时 m_isLivePhoto 未必为 true),
    // 槽内以 m_isLivePhoto 判断,避免普通视频播完误触发切图
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
        // teardownPlayer 断连后不会到此;但快速切换文件时旧信号可能迟到送达
        if (!m_player) return;
        // 媒体后端的加载/失效/卡住都会从这里过——卡死排查的关键轨迹
        // (全模式记录:#255 音频"不自动播放"这类申诉只有状态链才能归因)
        Logger::event(QStringLiteral("mediaStatus=%1 mode='%2' src='%3'")
                          .arg(int(status)).arg(m_mode)
                          .arg(m_player->source().toLocalFile()));
        // 延迟 attach:源装载就绪才接视频输出并开播(showVideo 埋下 m_pendingPlay)。
        // 顺序要害:先校验 source() 再消费 pending——切换瞬间,上一路媒体的
        // LoadedMedia 会迟到送达,若先消费再校验,pending 被白白丢掉,
        // 新源就绪后无人 attach:表现为"放不出来"或同源重播"只出声不出画"
        if (!m_pendingPlay.isEmpty()
            && (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
            if (m_player->source() != QUrl::fromLocalFile(m_pendingPlay)) {
                Logger::event(QStringLiteral("deferred attach: stale status, keep pending '%1'")
                                  .arg(m_pendingPlay));
            } else {
                const QString pending = m_pendingPlay;
                m_pendingPlay.clear();
                Logger::event(QStringLiteral("deferred attach+play '%1'").arg(pending));
                // #104:这里**不能** m_vw->show()。装载期 raiseVideoCover() 已经把
                // 视频控件藏起来了(唯一能挡住原生视频窗的办法),此刻放出来就等于
                // 把上一路的末帧又露出去。露出只由 revealVideo() 在首帧到达时做。
                // 隐藏期间媒体后端照常送帧(实测 2s 出 60 帧),不影响首帧检测。
                m_player->setVideoOutput(m_vw);
                m_videoOutAttached = true;
                // #104:装载期 raiseVideoCover() 的布防,收回权交给本路首帧
                armCoverUntilFirstFrame();
                if (m_isLivePhoto || (m_mode == "video" && pp_impl::s_bool("Viewer/autoPlayVideo", true))) {
                    m_player->play();
                    m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPause)));
                } else {
                    m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPlay)));
                }
            }
        }
        if (status == QMediaPlayer::InvalidMedia) {
            m_pendingPlay.clear();
            // 源失效:解除布防并露出视频控件(空态),而不是永远黑着
            revealVideo();
            // 音频源失效也走重封装自救(部分坏件后端直接报 Invalid,
            // 而非"装载正常但位置冻结"那种,两条路都进 startAudioRemux)
            if (m_mode == "audio") startAudioRemux();
        }
        if (status != QMediaPlayer::EndOfMedia) return;
        // setSource 已发→旧源还未完全退场:忽略过渡期 EndOfMedia,
        // 否则 stale 事件会错误触发 finishLivePhoto / loopVideo 重播
        if (!m_pendingPlay.isEmpty()) return;
        if (m_isLivePhoto) { Logger::event("live: EndOfMedia -> finish"); finishLivePhoto(); return; }
        // Viewer/loopVideo:普通视频播完按设置循环
        if (m_mode == "video" && pp_impl::s_bool("Viewer/loopVideo", false)) {
            m_player->setPosition(0);
            m_player->play();
        }
    });

    // 视频画面尺寸:首帧到达即记下并重铺视频面 —— 信封(四周留边)由 pvVideo
    // 的主题底色承担,QVideoWidget 是原生 D3D 画布,自己的信封黑边不吃调色板,
    // 浅色主题下必须是"我们铺比例、底色露主题"而不是让它满铺涂黑
    if (QVideoSink* vs = m_player->videoSink()) {
        connect(vs, &QVideoSink::videoFrameChanged, this,
                [this](const QVideoFrame& f) {
            if (!f.isValid() || f.size() == m_videoSize) return;
            m_videoSize = f.size();
            if (m_mode == "video") syncVideoChildren();
        });
    }

    // 播放器错误全模式落日志:后端拒绝解码/打不开文件时这是唯一痕迹
    connect(m_player, &QMediaPlayer::errorOccurred,
            this, [this](QMediaPlayer::Error err, const QString& msg) {
        if (!m_player) return;
        Logger::event(QStringLiteral("playerError=%1 '%2' mode='%3' src='%4'")
                          .arg(int(err)).arg(msg).arg(m_mode)
                          .arg(m_player->source().toLocalFile()));
    });

    // 注:m_btnPlay::clicked 与 m_progress::sliderMoved 已移出 setupPlayer,
    // 改在构造函数一次性建立。teardown→setup 循环每次都会新加一份,导致
    // 点一次播放切换 N 次(N=循环次数)、拖动进度条 N 倍 setPosition 调用。

    // 时长变化
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        if (!m_player) return;
        m_progress->setRange(0, static_cast<int>(dur));
        // 波形桶映射需要总时长:QMediaPlayer 这路毫秒→微秒补发给 worker
        // (QAudioDecoder 的 durationChanged 缺席时这是唯一分母来源;两路谁
        // 先到都收敛到同一值)
        if (m_mode == "audio" && m_waveWorker)
            QMetaObject::invokeMethod(m_waveWorker, "setTotalUs",
                                      Q_ARG(qint64, dur * 1000));
    });

    // 位置更新（Qt6 signal，替换 Qt5 timer）
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        if (!m_player) return;
        qint64 dur = m_player->duration();
        // 擦洗中别回写句柄,否则播放位置会把用户正拖着的滑块拽走
        if (dur > 0 && !m_progress->isSliderDown()) {
            m_progress->setValue(static_cast<int>(pos));
        }
        auto fmt = [](qint64 ms) -> QString {
            int sec = static_cast<int>(ms / 1000);
            // seek 期间 dur < pos:剩余时间为负,qMax 防止 "-X:X" 显示
            if (sec < 0) sec = 0;
            int h = sec / 3600;
            return h > 0
                ? QString("%1:%2:%3").arg(h).arg((sec % 3600) / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'))
                : QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
        };
        qint64 shown = m_timeRemaining ? qMax(qint64(0), dur - pos) : pos;
        m_timeLabel->setText(fmt(shown) + " / " + fmt(dur));
    });
}

void PreviewPanel::teardownPlayer() {
    if (!m_player) return;
    Logger::event(QStringLiteral("teardownPlayer src='%1'").arg(m_player->source().toLocalFile()));
    // 立即断开视频输出:阻止播放器继续往 QVideoWidget 渲染帧
    if (m_vw) {
        m_player->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
        m_videoOutAttached = false;
    }
    // 断开全部信号槽:deleteLater 延迟销毁前已注册的 queued 回调可能稍后才投递,
    // 必须断连以免回调读到 nullptr 成员(见 mediaStatusChanged/positionChanged/durationChanged)
    disconnect(m_player, nullptr, this, nullptr);
    m_player->stop();
    // deleteLater：对象在事件循环末尾销毁，避免同步 delete 的 UAF 风险
    m_player->deleteLater();
    m_player = nullptr;
    if (m_audioOutput) {
        m_audioOutput->deleteLater();
        m_audioOutput = nullptr;
    }
    // 销毁复用的视频控件(与播放器同生命周期;下次 showVideo 经 ensureVideoWidget 重建)
    if (m_vw) {
        m_vw->hide();
        delete m_vw;
        m_vw = nullptr;
    }
    if (m_videoCover) m_videoCover->hide();
    // 遮罩布防连的是 player 的 QVideoSink,不受上面 disconnect(m_player,...) 管辖
    disconnect(m_coverConn);
    m_coverArmed = false;
}

// #214:删除/移动/改名前释放句柄。命中判定含"删的是文件夹、正在播里面的文件"
// (前缀按 / 对齐;Windows 路径大小写不敏感,两种写法都要容)。只放句柄不换画面:
// 删除成功后调用方 reloadAfterDelete 自然换图;取消/失败场景画面停原地不算错。
// WaveForm 解码线程同样握着音频文件的读句柄,一并收。
void PreviewPanel::releaseFileLocks(const QStringList& paths) {
    if (paths.isEmpty()) return;
    auto hit = [&paths](const QString& f) {
        if (f.isEmpty()) return false;
        const QString af = QFileInfo(f).absoluteFilePath();
        for (const QString& p : paths) {
            const QString base = QFileInfo(p).absoluteFilePath();
            if (af.compare(base, Qt::CaseInsensitive) == 0) return true;
            if (af.startsWith(base + QLatin1Char('/'), Qt::CaseInsensitive)) return true;
        }
        return false;
    };
    if (!hit(m_filePath) && !hit(m_livePhotoOriginalPath)) return;
    Logger::event(QStringLiteral("releaseFileLocks '%1'").arg(m_filePath));
    stopMovie();
    teardownPlayer();
    teardownWave();
}

void PreviewPanel::clear() {
    Logger::event(QStringLiteral("clear (was '%1')").arg(m_filePath));
    teardownPlayer();
    stopMovie();
    cleanupExtractCache(); // 切换目录后，旧目录提取的临时视频不再需要
    delete m_origPix;
    m_origPix = nullptr;
    m_preloadQueue.clear();
    m_preloadCache.clear();
    m_preloadOrder.clear();
    m_imgLabel->clear();
    m_imgLabel->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_controlBar->hide();
    m_imgSpace->hide();
    m_placeholder->show();
    if (m_liveBadge) m_liveBadge->hide();
    m_mode = "none";
    m_scale = 1.0;
    m_filePath.clear();   // 屏上已空:filePath() 不能还谎报旧路径
    m_livePhotoOriginalPath.clear();
    m_isLivePhoto = false;
}

// 主题切换:音频/波形标签文字色是构造期内联样式表,按新色重灌;
// 自绘底色在 paintEvent 里即时取主题默认值,补一次重绘即换白;
// 播放控制栏图标与视频面遮罩同拍换色(图标深白反转/视频底随主题)
void PreviewPanel::refreshThemeColors() {
    update();
    if (m_audioLabel)
        m_audioLabel->setStyleSheet(
            QString("color:%1;font-size:16px;background:transparent;").arg(C_TEXT_SUB));
    if (m_waveLabel)
        m_waveLabel->setStyleSheet(
            QString("color:%1;font-size:12px;background:transparent;").arg(C_TEXT_DIM));
    // 控制栏四钮重染(播放/暂停按当前态选形;播放器不存在时取播放形)
    if (m_btnPrev)
        m_btnPrev->setIcon(pp_impl::themeIcon(
            style()->standardIcon(QStyle::SP_MediaSkipBackward)));
    if (m_btnStop)
        m_btnStop->setIcon(pp_impl::themeIcon(
            style()->standardIcon(QStyle::SP_MediaStop)));
    if (m_btnVolume)
        m_btnVolume->setIcon(pp_impl::themeIcon(
            style()->standardIcon(QStyle::SP_MediaVolume)));
    if (m_btnPlay)
        m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(
            m_player && m_player->playbackState() == QMediaPlayer::PlayingState
                ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay)));
    // 视频面/遮罩底色随主题(浏览器浅色=白);全屏经 backdropColor 恒黑
    applyVideoBackdrop();
}
