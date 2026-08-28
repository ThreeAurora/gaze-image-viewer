#include "previewpanel.h"
#include "livephoto.h"
#include "thumbnailer.h"
#include "logger.h"
#include "logger.h"
#include "wicdecode.h"
#include "settings.h"
#include "labelstore.h"
#include "markdown.h"
#include "pdfrender.h"
#include "imgproc.h"
#include "constants.h"
#include "viewerhotkeys.h"
#include "shelldelete.h"
#include "fileentry.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QMovie>
#include <QMovie>
#include <QPainter>
#include <QtMath>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QSplitter>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QMimeData>
#include <QMediaDevices>
#include <QWidgetAction>
#include <QThreadPool>
#include <QTextEdit>
#include <QFile>
#include <QTextEdit>
#include <QFile>
#include <QMetaObject>
#include <QToolTip>

// 标准图标染成白色(深色主题下 QStyle 图标是深色的)
static QIcon whiteIcon(const QIcon& base, int size = 32) {
    QPixmap pm = base.pixmap(size, size);
    QPixmap white(pm.size());
    white.fill(Qt::transparent);
    QPainter p(&white);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(white.rect(), QColor("#FFFFFF"));
    p.end();
    return QIcon(white);
}
#include <QWidgetAction>
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
    setMouseTracking(true);   // 悬停也要收移动事件:全屏隐藏指针后靠它恢复

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 空态占位(未选中任何文件时)
    m_placeholder = new QLabel;
    m_placeholder->setAlignment(Qt::AlignCenter);
    m_placeholder->setStyleSheet(
        QString("color:%1;font-size:13px;background:transparent;")
            .arg(C_TEXT_DIM));
    layout->addWidget(m_placeholder, 1);

    // 图片标签:不进布局——缩放/拖动需要自由定位,
    // 尺寸可超面板(超出部分裁剪,拖动=移动视口),否则放大后只剩"片段"
    m_imgLabel = new QLabel(this);
    m_imgLabel->setAlignment(Qt::AlignCenter);
    m_imgLabel->hide();

    // 音频标签
    m_audioLabel = new QLabel;
    m_audioLabel->setAlignment(Qt::AlignCenter);
    m_audioLabel->setStyleSheet(QString("color:%1;font-size:16px;background:transparent;").arg(C_TEXT_SUB));
    m_audioLabel->hide();
    layout->addWidget(m_audioLabel, 1);

    // txt 文本预览(等宽字体,只读,深色底)
    m_textEdit = new QTextEdit;
    m_textEdit->setReadOnly(true);
    m_textEdit->setStyleSheet(
        "QTextEdit{background:" C_CONTENT ";color:#E0E0E0;border:none;"
        "font-family:'Consolas','Courier New',monospace;font-size:13px;"
        "selection-background-color:" C_ACCENT ";}");
    m_textEdit->hide();
    layout->addWidget(m_textEdit, 1);

    // 视频区
    m_videoWidget = new QWidget;
    m_videoWidget->setStyleSheet("background:#0A0A0C;");
    m_videoWidget->hide();
    layout->addWidget(m_videoWidget, 1);

    // 控制栏(XnView 排布):上一文件 / 播放暂停 / 停止 / 音量 / 进度 / 时间
    m_controlBar = new QWidget;
    m_controlBar->setFixedHeight(40);
    m_controlBar->setStyleSheet(
        "background:" C_TOOLBAR ";border-top:1px solid " C_SEPARATOR ";");
    m_controlBar->hide();

    auto* cl = new QHBoxLayout(m_controlBar);
    cl->setContentsMargins(10, 4, 10, 4);
    cl->setSpacing(4);

    // 按钮统一样式 + 白色图标:播放/暂停图标在别处切换时必须同样走 whiteIcon,
    // 否则标准图标自带深色,在深底上直接"变黑看不见"
    const char* btnQss =
        "QPushButton{background:transparent;border:none;border-radius:4px;padding:4px;}"
        "QPushButton:hover{background:" C_CARD_HOVER ";}"
        "QToolButton{background:transparent;border:none;border-radius:4px;padding:4px;}"
        "QToolButton:hover{background:" C_CARD_HOVER ";}";
    auto mkBtn = [&](auto* b, QStyle::StandardPixmap sp, int w, const QString& tip) {
        b->setIcon(pp_impl::whiteIcon(style()->standardIcon(sp)));
        b->setIconSize(QSize(16, 16));
        b->setStyleSheet(btnQss);
        b->setFixedSize(w, 28);
        b->setToolTip(tip);
        cl->addWidget(b);
    };

    m_btnPrev = new QPushButton;
    mkBtn(m_btnPrev, QStyle::SP_MediaSkipBackward, 30,
          QString::fromUtf8("\xe4\xb8\x8a\xe4\xb8\x80\xe4\xb8\xaa\xe6\x96\x87\xe4\xbb\xb6")); // 上一个文件
    connect(m_btnPrev, &QPushButton::clicked, this, [this]() { emit navFile(-1); });

    // 播放/暂停:点击槽在 setupPlayer(播放器只建一次,连接也只建一次)
    m_btnPlay = new QPushButton;
    mkBtn(m_btnPlay, QStyle::SP_MediaPlay, 32,
          QString::fromUtf8("\xe6\x92\xad\xe6\x94\xbe/\xe6\x9a\x82\xe5\x81\x9c")); // 播放/暂停

    m_btnStop = new QPushButton;
    mkBtn(m_btnStop, QStyle::SP_MediaStop, 30,
          QString::fromUtf8("\xe5\x81\x9c\xe6\xad\xa2(\xe5\x9b\x9e\xe5\x88\xb0\xe5\xbc\x80\xe5\xa4\xb4)")); // 停止(回到开头)
    connect(m_btnStop, &QPushButton::clicked, this, [this]() {
        if (!m_player) return;
        m_player->stop();          // Qt6 stop 同时把位置归零 → 再播从头开始
        m_progress->setValue(0);
    });

    m_btnVolume = new QToolButton;
    mkBtn(m_btnVolume, QStyle::SP_MediaVolume, 32,
          QString::fromUtf8("\xe9\x9f\xb3\xe9\x87\x8f"));   // 音量

    m_progress = new QSlider(Qt::Horizontal);
    m_progress->setRange(0, 0);
    m_progress->setFixedHeight(16);
    m_progress->setMouseTracking(true);   // hover 移动即请求秒级缩略图
    m_progress->setStyleSheet(
        "QSlider::groove:horizontal{height:3px;background:" C_SEPARATOR ";border-radius:1px;}"
        "QSlider::sub-page:horizontal{background:" C_ACCENT ";border-radius:1px;}"
        "QSlider::add-page:horizontal{background:" C_SEPARATOR ";border-radius:1px;}"
        "QSlider::handle:horizontal{width:9px;height:9px;margin:-3px 0;"
        "background:#FFFFFF;border-radius:4px;}"
        "QSlider::handle:horizontal:hover{background:#DCE7FF;}");
    m_progress->setToolTip(QString::fromUtf8(
        "\xe7\x82\xb9\xe5\x87\xbb\xe6\x97\xb6\xe9\x97\xb4\xe8\xbd\xb4\xe4\xbb\xbb\xe6\x84\x8f\xe4\xbd\x8d\xe7\xbd\xae\xe8\xb7\xb3\xe8\xbd\xac")); // 点击时间轴任意位置跳转
    m_progress->installEventFilter(this);   // 播放条点击直接跳转
    cl->addWidget(m_progress, 1);

    m_timeLabel = new QLabel("0:00 / 0:00");
    m_timeLabel->setStyleSheet(
        QString("color:%1;font-size:12px;background:transparent;").arg(C_TEXT));
    m_timeLabel->setFixedWidth(120);
    m_timeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_timeLabel->setToolTip(QString::fromUtf8(
        "\xe7\x82\xb9\xe5\x87\xbb\xe5\x88\x87\xe6\x8d\xa2 \xe5\xb7\xb2\xe6\x92\xad/\xe5\x89\xa9\xe4\xbd\x99\xe6\x97\xb6\xe9\x97\xb4")); // 点击切换 已播/剩余时间
    m_timeLabel->installEventFilter(this);  // 点击切换剩余时间显示
    cl->addWidget(m_timeLabel);

    layout->addWidget(m_controlBar);

    // LIVE 徽章(动态照片播放时的右上角标识,child of videoWidget)
    m_liveBadge = new QLabel("LIVE", m_videoWidget);
    m_liveBadge->setStyleSheet(
        "QLabel{background:rgba(0,0,0,150);color:#FFF;border-radius:8px;"
        "padding:2px 10px;font-size:11px;font-weight:bold;"
        "border:1px solid rgba(255,255,255,60);}");
    m_liveBadge->adjustSize();
    m_liveBadge->hide();

    // 事件过滤器
    m_imgLabel->installEventFilter(this);
    m_videoWidget->installEventFilter(this);   // 视频区左键=播放/暂停(见 eventFilter)
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
        val->setFixedWidth(30);
        val->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
        val->setStyleSheet(
            QString("QLabel{background:transparent;color:%1;font-size:13px;}").arg(C_TEXT));
        wl->addWidget(slider);
        wl->addWidget(val);
        connect(slider, &QSlider::valueChanged, this, [this, val](int v) {
            if (m_audioOutput) m_audioOutput->setVolume(v / 100.0);
            val->setText(QString::number(v));
            m_btnVolume->setToolTip(
                QString::fromUtf8("音量 %1").arg(v));
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
        sb->setStyleSheet(
            "QScrollBar{background:rgba(20,20,24,200);border:none;margin:0;}"
            "QScrollBar::handle{background:#5A5A62;border-radius:3px;}"
            "QScrollBar::handle:hover{background:#7A7A82;}"
            "QScrollBar::add-line,QScrollBar::sub-line{height:0;width:0;}");
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

    // ── Fullscreen/showInfo:左上角文件信息条 ──
    m_infoLabel = new QLabel(this);
    m_infoLabel->setStyleSheet(
        "QLabel{background:rgba(10,10,14,190);color:#E0E0E0;font-size:12px;"
        "padding:6px 10px;border-radius:4px;}");
    m_infoLabel->hide();

    // ── Fullscreen/showToolbar:全屏浮动工具条(上一/下一/适应/1:1/退出) ──
    m_floatBar = new QWidget(this);
    m_floatBar->setStyleSheet(
        "QWidget{background:rgba(18,18,22,225);border:1px solid #3A3A42;border-radius:6px;}");
    m_floatBar->hide();
    {
        auto* fl = new QHBoxLayout(m_floatBar);
        fl->setContentsMargins(6, 4, 6, 4);
        fl->setSpacing(4);
        const char* bq =
            "QPushButton{background:transparent;border:none;border-radius:4px;"
            "padding:3px;color:#E8E8E8;min-width:26px;}"
            "QPushButton:hover{background:#3A3A42;}";
        auto add = [&](QStyle::StandardPixmap sp, const QString& tip, auto&& fn) {
            auto* b = new QPushButton;
            b->setIcon(pp_impl::whiteIcon(style()->standardIcon(sp)));
            b->setToolTip(tip);
            b->setStyleSheet(bq);
            QObject::connect(b, &QPushButton::clicked, this, fn);
            fl->addWidget(b);
        };
        add(QStyle::SP_MediaSkipBackward, QString::fromUtf8("上一个文件"),
            [this]() { emit navFile(-1); });
        add(QStyle::SP_MediaSkipForward, QString::fromUtf8("下一个文件"),
            [this]() { emit navFile(1); });
        add(QStyle::SP_DialogResetButton, QString::fromUtf8("适应窗口"),
            [this]() { fitAuto(); });
        add(QStyle::SP_FileDialogDetailedView, QString::fromUtf8("1:1"),
            [this]() { if (m_origPix) { m_scale = oneToOneScale(); m_ctrlZoomed = true; render(); } });
        add(QStyle::SP_DialogCloseButton, QString::fromUtf8("退出全屏"),
            [this]() { if (inFullscreen()) window()->showNormal(); });
    }

    // ── Viewer/panTool:右下角平移导航小窗(图溢出视口时才出现) ──
    m_panTool = new QWidget(this);
    m_panTool->setFixedSize(150, 110);
    m_panTool->setStyleSheet(
        "QWidget{background:rgba(14,14,18,220);border:1px solid #3A3A42;}");
    m_panTool->hide();
    m_panThumb = new QLabel(m_panTool);
    m_panThumb->setAlignment(Qt::AlignCenter);
    m_panThumb->setGeometry(1, 1, 148, 108);
    m_panView = new QWidget(m_panTool);
    m_panView->setStyleSheet("background:transparent;border:1px solid #4C9AF5;");
    m_panView->hide();
    // 拖动蓝框/缩略图 → 视口跟随(事件过滤器在 eventFilter 里处理)
    m_panThumb->setCursor(Qt::PointingHandCursor);
    m_panThumb->installEventFilter(this);
    m_panView->installEventFilter(this);
    // 拖动蓝框/缩略图 → 视口跟随(事件过滤器在 eventFilter 里处理)
    m_panThumb->setCursor(Qt::PointingHandCursor);
    m_panThumb->installEventFilter(this);
    m_panView->installEventFilter(this);

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
    teardownPlayer();
    cleanupExtractCache();
}

// ═══════════════════════════════════════════
// 设置活接线:查看器/全屏页面的选项改动即时生效(无 need-restart)
//   读取一律走 AppSettings,不缓存 —— 热路径(逐帧 render)不碰 ini,
//   只在 fit/backdrop 这类低频时机取值
// ═══════════════════════════════════════════
static bool s_bool(const QString& k, bool def) {
    return AppSettings::instance().get(k, def).toBool();
}
static int s_int(const QString& k, int def) {
    return AppSettings::instance().get(k, def).toInt();
}

bool PreviewPanel::inFullscreen() const {
    const QWidget* w = window();
    return w && w->isFullScreen();
}

// 同一角色在两处各有一套设置:全屏时改用 Fullscreen/*,否则 Viewer/*
QString PreviewPanel::modeKey(const char* suffix) const {
    return QString::fromLatin1(inFullscreen() ? "Fullscreen/" : "Viewer/")
         + QString::fromLatin1(suffix);
}

QColor PreviewPanel::backdropColor() const {
    // 查看器与浏览器预览窗格用各自的背景色设置(XnView 同)
    const QString key = (m_viewerMode || inFullscreen())
        ? modeKey("backColor")
        : QStringLiteral("Browser/previewBackColor");
    QColor c(AppSettings::instance().get(key, QStringLiteral("#000000")).toString());
    return c.isValid() ? c : QColor("#000000");
}

// 透明像素下的挡板底纹(Viewer/checkerMode):16px 两色方格
static QImage checkerTile(const QColor& base) {
    const int cell = 8;
    QImage img(cell * 2, cell * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(base);
    QPainter p(&img);
    QColor ink = base.lightness() > 128 ? base.darker(140) : base.lighter(160);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawRect(0, 0, cell, cell);
    p.drawRect(cell, cell, cell, cell);
    p.end();
    return img;
}

void PreviewPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), backdropColor());
    if (s_bool("Viewer/checkerMode", false)) {
        QBrush tile(checkerTile(backdropColor()));
        tile.setStyle(Qt::TexturePattern);
        p.fillRect(rect(), tile);
    }

    // Viewer/selectedOverlay:画面构图辅助线(0 正常=不画 1 三分法 2 黄金分割)
    // 画在图片标签的几何范围内 —— 图片是自由定位的,坐标取它的当前位置
    const int guide = s_int("Viewer/selectedOverlay", 0);
    if (guide > 0 && m_mode == "image" && m_imgLabel->isVisible()) {
        const QRect r = m_imgLabel->geometry();
        if (r.width() > 40 && r.height() > 40) {
            p.setPen(QPen(QColor(255, 255, 255, 110), 1, Qt::DotLine));
            const double f1 = (guide == 1) ? 1.0 / 3.0 : 1.0 - 0.618;
            const double f2 = 1.0 - f1;
            for (double f : {f1, f2}) {
                const int x = r.x() + int(r.width()  * f);
                const int y = r.y() + int(r.height() * f);
                p.drawLine(x, r.y(), x, r.bottom());
                p.drawLine(r.x(), y, r.right(), y);
            }
            if (guide == 2) {   // 黄金分割再补两条对角线方向的螺旋基准线
                p.setPen(QPen(QColor(255, 255, 255, 70), 1, Qt::DotLine));
                p.drawLine(r.topLeft(), r.bottomRight());
                p.drawLine(r.topRight(), r.bottomLeft());
            }
        }
    }
}

// 背景色/挡板/图片边框变更时调用(构造 + AppSettings::changed)
void PreviewPanel::applyBackdrop() {
    update();
    // Viewer/showBorder:图片外框(默认关)
    const bool border = s_bool("Viewer/showBorder", false);
    m_imgLabel->setStyleSheet(border
        ? QStringLiteral("QLabel{border:1px solid #FFFFFF;background:transparent;}")
        : QStringLiteral("QLabel{background:transparent;}"));
}

void PreviewPanel::setViewerMode(bool on) {
    if (m_viewerMode == on) return;
    m_viewerMode = on;
    applyBackdrop();
    applyViewerChrome();
}

// ═══════════════════════════════════════════
// 设置活接线:查看器/全屏界面元素
// ═══════════════════════════════════════════
void PreviewPanel::applyViewerChrome() {
    updateOverlayScrollbars();
    updateInfoBar();
    updateFloatBar();
    updatePanTool();
    updateSelectionHighlight();
    updateRatingBadge();
}

// Viewer|Fullscreen/showScrollbar:图比视口大时才出现,位置贴边浮在图上
void PreviewPanel::updateOverlayScrollbars() {
    const bool on = m_mode == "image" && m_origPix
                    && s_bool(modeKey("showScrollbar"), false);
    if (!on) {
        m_hScroll->hide();
        m_vScroll->hide();
        return;
    }
    const int iw = m_imgLabel->width(), ih = m_imgLabel->height();
    const int bw = width(), bh = height();
    const bool needH = iw > bw, needV = ih > bh;
    const int thick = 10;
    if (needH) {
        m_hScroll->setGeometry(0, bh - thick, bw - (needV ? thick : 0), thick);
        m_hScroll->setRange(0, iw - bw);
        m_hScroll->setPageStep(bw);
        m_hScroll->setSingleStep(24);
        m_hScroll->setValue(qBound(0, -m_imgLabel->x(), iw - bw));
        m_hScroll->show();
    } else m_hScroll->hide();
    if (needV) {
        m_vScroll->setGeometry(bw - thick, 0, thick, bh - (needH ? thick : 0));
        m_vScroll->setRange(0, ih - bh);
        m_vScroll->setPageStep(bh);
        m_vScroll->setSingleStep(24);
        m_vScroll->setValue(qBound(0, -m_imgLabel->y(), ih - bh));
        m_vScroll->show();
    } else m_vScroll->hide();
}

// Fullscreen/showInfo:全屏时左上角显示文件名/尺寸/缩放
// 文件名/尺寸/体积按文件缓存(QFileInfo::size() 是 stat 系统调用,
// 拖动窗口时 resize 每帧都进来,不能反复问磁盘),缩放百分比单独拼
void PreviewPanel::updateInfoBar() {
    const bool on = inFullscreen() && s_bool("Fullscreen/showInfo", true)
                    && !m_filePath.isEmpty();
    if (!on) { m_infoLabel->hide(); return; }
    if (m_infoFileKey != m_filePath) {
        m_infoFileKey = m_filePath;
        QFileInfo fi(m_filePath);
        m_infoBase = fi.fileName()
                   + (m_origPix ? QString("  %1x%2")
                          .arg(m_origPix->width()).arg(m_origPix->height()) : QString())
                   + "  " + formatSize(fi.size());
    }
    m_infoLabel->setText(QString::fromUtf8("%1  %2%")
        .arg(m_infoBase).arg(int(m_scale * 100)));
    m_infoLabel->adjustSize();
    m_infoLabel->move(12, 12);
    m_infoLabel->raise();
    m_infoLabel->show();
}

// Fullscreen/showToolbar(常显) + Fullscreen/floatView(鼠标移到顶侧/右侧才浮现)
void PreviewPanel::updateFloatBar(const QPoint* cursor) {
    if (!inFullscreen()) { m_floatBar->hide(); return; }
    const bool always = s_bool("Fullscreen/showToolbar", false);
    const bool floating = s_bool("Fullscreen/floatView", true);
    bool show = always;
    if (!show && floating && cursor) {
        const int edge = 48;
        show = cursor->y() <= edge || cursor->x() >= width() - edge;
    }
    if (!show) { m_floatBar->hide(); return; }
    m_floatBar->adjustSize();
    m_floatBar->move((width() - m_floatBar->width()) / 2, 10);
    m_floatBar->raise();
    m_floatBar->show();
}

// Viewer/showRating:查看器右上角显示当前文件的颜色标记(程序无独立评级数据模型,
// 这里如实呈现 Gaze 实际拥有的"颜色标记",不假装显示星级)
void PreviewPanel::updateRatingBadge() {
    const bool on = s_bool("Viewer/showRating", true) && !m_filePath.isEmpty();
    if (!on) { if (m_ratingDot) m_ratingDot->hide(); return; }
    const int color = LabelStore::instance().colorFor(m_filePath);
    if (color <= 0) { if (m_ratingDot) m_ratingDot->hide(); return; }
    if (!m_ratingDot) {
        m_ratingDot = new QLabel(this);
        m_ratingDot->setFixedSize(14, 14);
    }
    m_ratingDot->setStyleSheet(
        QString("QLabel{background:%1;border:1px solid #FFFFFF;border-radius:7px;}")
            .arg(LabelStore::colorValue(color).name()));
    m_ratingDot->move(width() - 24, 12);
    m_ratingDot->raise();
    m_ratingDot->show();
}

// Viewer/panTool:右下角导航小窗(缩略图 + 当前视口框)。仅图溢出视口时出现
void PreviewPanel::updatePanTool() {
    const bool on = s_bool("Viewer/panTool", true) && m_mode == "image" && m_origPix
                    && (m_imgLabel->width() > width() || m_imgLabel->height() > height());
    if (!on) { m_panTool->hide(); return; }

    const int boxW = m_panThumb->width() - 4, boxH = m_panThumb->height() - 4;
    if (m_panKey != m_filePath) {
        m_panThumb->setPixmap(m_origPix->scaled(boxW, boxH, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
        m_panKey = m_filePath;
    }
    // 蓝框=视口在整图中的位置。几何必须与拖动映射(panNavTo)完全一致:
    // 都基于缩略图 pixmap 的实际摆放(KeepAspectRatio 居中,可能留边),
    // 否则拖动时蓝框不落在指尖下。旧实现按整个 box 映射,留边时框会偏
    const QPixmap tp = m_panThumb->pixmap();
    if (!tp.isNull()) {
        const int ox = (m_panThumb->width() - tp.width()) / 2;
        const int oy = (m_panThumb->height() - tp.height()) / 2;
        const double sx = double(m_imgLabel->width()) / m_origPix->width();
        const double sy = double(m_imgLabel->height()) / m_origPix->height();
        const double vx0 = qMax(0.0, -double(m_imgLabel->x())) / sx;   // 视口左缘的图像 x
        const double vy0 = qMax(0.0, -double(m_imgLabel->y())) / sy;
        const double rw = qMin<double>(1.0, width()  / sx / m_origPix->width())  * tp.width();
        const double rh = qMin<double>(1.0, height() / sy / m_origPix->height()) * tp.height();
        m_panView->setGeometry(ox + int(vx0 / m_origPix->width() * tp.width()),
                               oy + int(vy0 / m_origPix->height() * tp.height()),
                               qMax(4, int(rw)), qMax(4, int(rh)));
    }
    m_panView->show();
    m_panView->raise();
    m_panTool->move(width() - m_panTool->width() - 12, height() - m_panTool->height() - 12);
    m_panTool->raise();
    m_panTool->show();
}

// 导航小窗拖动:指尖下的缩略图点 → 映射回整图坐标 → 让视口中心对准它。
// 按住蓝框(或缩略图任意处)拖动,蓝框始终跟指尖走,可快速甩到图片任意角落
void PreviewPanel::panNavTo(const QPoint& thumbPos) {
    const QPixmap tp = m_panThumb->pixmap();
    if (!m_origPix || tp.isNull()) return;
    const int ox = (m_panThumb->width() - tp.width()) / 2;
    const int oy = (m_panThumb->height() - tp.height()) / 2;
    const double nx = qBound(0.0, double(thumbPos.x() - ox) / tp.width(), 1.0);
    const double ny = qBound(0.0, double(thumbPos.y() - oy) / tp.height(), 1.0);
    // label.x + nx*labelW = 视口中线  →  label.x = 中线 - nx*labelW
    const QPoint pos(width() / 2 - int(nx * m_imgLabel->width()),
                     height() / 2 - int(ny * m_imgLabel->height()));
    m_imgLabel->move(clampedLabelPos(pos));
    updateOverlayScrollbars();
    updatePanTool();
}

// Viewer/highlightSelection:查看器里给当前图片加一层强调框(选中高亮)
// setStyleSheet 会触发样式重算,而本函数在 resize/切文件时都会被调,
// 所以按最终形态缓存,值没变就一个字节都不碰控件
void PreviewPanel::updateSelectionHighlight() {
    const bool hl     = s_bool("Viewer/highlightSelection", true) && m_mode == "image";
    const bool border = s_bool("Viewer/showBorder", false);
    const int  want   = border ? 2 : (hl ? 1 : 0);
    if (want == m_labelStyleState) return;
    m_labelStyleState = want;
    switch (want) {
    case 1:
        m_imgLabel->setStyleSheet(
            QStringLiteral("QLabel{background:transparent;border:1px solid #4C9AF5;}"));
        break;
    case 2:
        m_imgLabel->setStyleSheet(
            QStringLiteral("QLabel{border:1px solid #FFFFFF;background:transparent;}"));
        break;
    default:
        m_imgLabel->setStyleSheet(QStringLiteral("QLabel{background:transparent;}"));
        break;
    }
}

// Viewer/pixelRatio:非正方形像素的显示宽高比
double PreviewPanel::pixelAspect() const {
    static const double ratios[] = {1.00, 0.91, 0.95, 1.09, 1.20,
                                    1.33, 1.46, 1.50, 1.90, 2.00};
    const int i = qBound(0, s_int("Viewer/pixelRatio", 0), 9);
    return ratios[i];
}

// Viewer/autoPlayAudioCompanion:图片旁存在同名音频时自动播放
void PreviewPanel::playAudioCompanion(const QString& imagePath) {
    if (!s_bool("Viewer/autoPlayAudioCompanion", false)) return;
    QFileInfo fi(imagePath);
    static const char* audioExts[] = {".mp3", ".wav", ".flac", ".m4a", ".ogg", ".aac"};
    for (const char* ext : audioExts) {
        const QString side = fi.absolutePath() + "/" + fi.completeBaseName() + ext;
        if (!QFileInfo::exists(side)) continue;
        setupPlayer();
        if (!m_player) return;
        m_player->setSource(QUrl::fromLocalFile(side));
        m_player->play();
        return;
    }
}

// Viewer/zoomMode = 0(固定):缩放在预设档位之间跳,不再连续无级变化
double PreviewPanel::stepZoom(double cur, bool up) const {
    static const double steps[] = {0.05, 0.10, 0.16, 0.25, 0.33, 0.50, 0.66, 0.75,
                                   1.00, 1.50, 2.00, 3.00, 4.00, 6.00, 8.00, 10.00};
    constexpr int n = int(sizeof(steps) / sizeof(steps[0]));
    if (up) {
        for (int i = 0; i < n; ++i)
            if (steps[i] > cur + 1e-6) return steps[i];
        return steps[n - 1];
    }
    for (int i = n - 1; i >= 0; --i)
        if (steps[i] < cur - 1e-6) return steps[i];
    return steps[0];
}

// Viewer/hidpiPixel:1:1 语义(默认关 = 1 图像像素 : 1 逻辑像素,与改造前一致)
double PreviewPanel::oneToOneScale() const {
    double s = 1.0;
    if (s_bool("Viewer/hidpiPixel", false)) {
        const double dpr = devicePixelRatioF();
        s = dpr > 0.01 ? 1.0 / dpr : 1.0;
    }
    // General/exifDpi:按 EXIF/JFIF 标称 DPI 还原物理尺寸(300dpi 的图 1:1 时
    // 按 300/96 ≈ 3.13 倍显示)。dpiAdjust 关时 X/Y 不等也统一用 X,保住长宽比
    if (s_bool("General/exifDpi", true) && (m_dpiX > 0 || m_dpiY > 0)) {
        const double dx = m_dpiX > 0 ? m_dpiX : 96.0;
        const double dy = (s_bool("General/dpiAdjust", true) && m_dpiY > 0) ? m_dpiY : dx;
        s *= (dx + dy) / 2.0 / 96.0;
    }
    return s;
}

// Viewer/autoFit 取值语义:
//   0 上次使用过的   1 不缩放(1:1)      2 适应窗口(默认)
//   3 仅放大小图     4 仅缩小大图        5 适应宽度
//   6 适应高度       7 适应宽或高(取大)  8 适应桌面
//   9 窗口适应图像   → 按"适应窗口"处理(需要改变窗口尺寸,查看器布局尚未支持)
double PreviewPanel::fitScaleFor(const QSize& viewSize) const {
    const double sw = double(viewSize.width())  / m_origPix->width();
    const double sh = double(viewSize.height()) / m_origPix->height();
    const double fit = std::min(sw, sh);
    switch (s_int(modeKey("autoFit"), 2)) {
    // Viewer/resetAutoOnNav:切文件时丢掉"上次使用过的"缩放,重新按自动模式算
    case 0: {
        const bool reset = s_bool("Viewer/resetAutoOnNav", false) && m_navigating;
        return (!reset && m_lastScale > 0) ? m_lastScale : fit;
    }
    // Viewer/hidpiPixel:开=1 图像像素映射到 1 物理像素(HiDPI 下画面变小但最锐利)
    case 1: return oneToOneScale();
    case 3: return std::max(1.0, fit);   // 小图放大到适应,大图保持 1:1
    case 4: return std::min(1.0, fit);   // 大图缩小到适应,小图保持 1:1
    case 5: return sw;
    case 6: return sh;
    case 7: return std::max(sw, sh);
    case 8: {
        const QRect av = QApplication::primaryScreen()->availableGeometry();
        return std::min(double(av.width())  / m_origPix->width(),
                        double(av.height()) / m_origPix->height());
    }
    default: return fit;
    }
}

// 拖拽平移约束(临时 1:1 放大与 Ctrl 缩放态通用):
//   图片某轴 ≤ 预览框 → 该轴锁死居中(两侧黑边等宽,不能挪动)
//   图片某轴 > 预览框 → 允许平移,但图片边缘不进入框内(平到顶即停,不露白边)
QPoint PreviewPanel::clampedLabelPos(QPoint p) const {
    const int imgW = m_imgLabel->width();
    const int imgH = m_imgLabel->height();
    const int boxW = width();
    const int boxH = height();
    if (imgW <= boxW)
        p.setX((boxW - imgW) / 2);
    else
        p.setX(qBound(boxW - imgW, p.x(), 0));
    if (imgH <= boxH)
        p.setY((boxH - imgH) / 2);
    else
        p.setY(qBound(boxH - imgH, p.y(), 0));
    return p;
}

// 拖拽平移约束(临时 1:1 放大与 Ctrl 缩放态通用):
//   图片某轴 ≤ 预览框 → 该轴锁死居中(两侧黑边等宽,不能挪动)
//   图片某轴 > 预览框 → 允许平移,但图片边缘不进入框内(平到顶即停,不露白边)
QPoint PreviewPanel::clampedLabelPos(QPoint p) const {
    const int imgW = m_imgLabel->width();
    const int imgH = m_imgLabel->height();
    const int boxW = width();
    const int boxH = height();
    if (imgW <= boxW)
        p.setX((boxW - imgW) / 2);
    else
        p.setX(qBound(boxW - imgW, p.x(), 0));
    if (imgH <= boxH)
        p.setY((boxH - imgH) / 2);
    else
        p.setY(qBound(boxH - imgH, p.y(), 0));
    return p;
}

// ═══════════════════════════════════════════
// 设置活接线:查看器/全屏页面的选项改动即时生效(无 need-restart)
//   读取一律走 AppSettings,不缓存 —— 热路径(逐帧 render)不碰 ini,
//   只在 fit/backdrop 这类低频时机取值
// ═══════════════════════════════════════════
static bool s_bool(const QString& k, bool def) {
    return AppSettings::instance().get(k, def).toBool();
}
static int s_int(const QString& k, int def) {
    return AppSettings::instance().get(k, def).toInt();
}

bool PreviewPanel::inFullscreen() const {
    const QWidget* w = window();
    return w && w->isFullScreen();
}

// 同一角色在两处各有一套设置:全屏时改用 Fullscreen/*,否则 Viewer/*
QString PreviewPanel::modeKey(const char* suffix) const {
    return QString::fromLatin1(inFullscreen() ? "Fullscreen/" : "Viewer/")
         + QString::fromLatin1(suffix);
}

QColor PreviewPanel::backdropColor() const {
    // 查看器与浏览器预览窗格用各自的背景色设置(XnView 同)
    const QString key = (m_viewerMode || inFullscreen())
        ? modeKey("backColor")
        : QStringLiteral("Browser/previewBackColor");
    QColor c(AppSettings::instance().get(key, QStringLiteral("#000000")).toString());
    return c.isValid() ? c : QColor("#000000");
}

// 透明像素下的挡板底纹(Viewer/checkerMode):16px 两色方格
static QImage checkerTile(const QColor& base) {
    const int cell = 8;
    QImage img(cell * 2, cell * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(base);
    QPainter p(&img);
    QColor ink = base.lightness() > 128 ? base.darker(140) : base.lighter(160);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawRect(0, 0, cell, cell);
    p.drawRect(cell, cell, cell, cell);
    p.end();
    return img;
}

void PreviewPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), backdropColor());
    if (s_bool("Viewer/checkerMode", false)) {
        QBrush tile(checkerTile(backdropColor()));
        tile.setStyle(Qt::TexturePattern);
        p.fillRect(rect(), tile);
    }
}

// 背景色/挡板/图片边框变更时调用(构造 + AppSettings::changed)
void PreviewPanel::applyBackdrop() {
    update();
    // Viewer/showBorder:图片外框(默认关)
    const bool border = s_bool("Viewer/showBorder", false);
    m_imgLabel->setStyleSheet(border
        ? QStringLiteral("QLabel{border:1px solid #FFFFFF;background:transparent;}")
        : QStringLiteral("QLabel{background:transparent;}"));
}

void PreviewPanel::setViewerMode(bool on) {
    if (m_viewerMode == on) return;
    m_viewerMode = on;
    applyBackdrop();
}

// Viewer/autoFit 取值语义:
//   0 上次使用过的   1 不缩放(1:1)      2 适应窗口(默认)
//   3 仅放大小图     4 仅缩小大图        5 适应宽度
//   6 适应高度       7 适应宽或高(取大)  8 适应桌面
//   9 窗口适应图像   → 按"适应窗口"处理(需要改变窗口尺寸,查看器布局尚未支持)
double PreviewPanel::fitScaleFor(const QSize& viewSize) const {
    const double sw = double(viewSize.width())  / m_origPix->width();
    const double sh = double(viewSize.height()) / m_origPix->height();
    const double fit = std::min(sw, sh);
    switch (s_int(modeKey("autoFit"), 2)) {
    case 0: return m_lastScale > 0 ? m_lastScale : fit;
    case 1: return 1.0;
    case 3: return std::max(1.0, fit);   // 小图放大到适应,大图保持 1:1
    case 4: return std::min(1.0, fit);   // 大图缩小到适应,小图保持 1:1
    case 5: return sw;
    case 6: return sh;
    case 7: return std::max(sw, sh);
    case 8: {
        const QRect av = QApplication::primaryScreen()->availableGeometry();
        return std::min(double(av.width())  / m_origPix->width(),
                        double(av.height()) / m_origPix->height());
    }
    default: return fit;
    }
}

void PreviewPanel::setViewerMode(bool on) {
    if (m_viewerMode == on) return;
    m_viewerMode = on;
    applyBackdrop();
    applyViewerChrome();
}

void PreviewPanel::loadFile(const QString& path) {
    // 换文件(或清空):递增代号,作废任何在途的后台解码结果
    ++m_imgReqGen;
    m_liveInfo.reset();
    m_pendingPlay.clear();   // 离开当前文件:未决的"装载后接输出"作废
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

    // 目录:预览内容 2x2 拼贴(与网格卡片同一管线),不再落"无预览"占位
    if (fi.isDir()) { showDirPreview(path); return; }
    stopMovie();   // 任何类型切换都先回收 GIF 动画(防泄漏/防隐藏继续解码)

    // 立即清屏:杜绝上一文件(尤其图片→视频切换)残影闪帧
    m_imgLabel->clear();
    m_imgLabel->hide();
    cancelHoverExtract();       // 换文件:取消进行中的 hover 抽帧并清缓存
    m_hoverCache.clear();
    QString ext = fi.suffix().toLower();

    teardownPlayer();

    // ── Live Photo 检测（在扩展名路由之前）──
    if (IMAGE_EXTS.count("." + ext)) {
        auto liveInfo = LivePhoto::detect(path);
        if (liveInfo) {
            m_liveInfo = liveInfo;   // 播完回静态图后,单击靠它重播
            m_liveInfo = liveInfo;   // 播完回静态图后,单击靠它重播
            QString videoPath;
            if (liveInfo->embedded && liveInfo->videoOffset >= 0) {
                // 内嵌型：优先复用本会话已提取的临时文件，避免反复 remux + %TEMP% 堆积
                if (m_extractCache.contains(path)) {
                    videoPath = m_extractCache.value(path);
                } else {
                    // 首次遇到:ffmpeg remux 秒级,改为后台提取——
                    // 先显示静态图(jpg 本身完整可显),提取完成自动切播放
                    showImage(path);
                    startExtractAsync(path, *liveInfo);
                    return;
                }
            } else if (!liveInfo->embedded) {
                videoPath = liveInfo->videoPath; // companion 配对型
            }
            if (!videoPath.isEmpty() && QFileInfo::exists(videoPath)) {
                m_livePhotoOriginalPath = path;
                m_isLivePhoto = true;
                showVideo(videoPath);
                return;
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
    } else if (ext == "md" && pp_impl::s_bool("Preview/showMd", false)) {
        showMarkdown(path);
    } else if (ext == "pdf" && pp_impl::s_bool("Preview/showPdf", false)) {
        showPdf(path);
    } else {
        m_mode = "none";
        if (m_player) m_player->stop();   // 非媒体文件:停播放(实例保留复用)
        m_placeholder->show();
        m_imgLabel->hide();
        m_videoWidget->hide();
        m_audioLabel->hide();
        m_textEdit->hide();
        m_controlBar->hide();
    }
}

// 文件夹预览:后台生成内容 2x2 拼贴(Thumbnailer 同一管线,含缓存)。
// 进目录默认选中第一项——首个是文件夹时,预览窗格也该有内容
void PreviewPanel::showNoPreview() {
    m_mode = "none";
    if (m_player) m_player->stop();
    if (m_liveBadge) m_liveBadge->hide();
    m_placeholder->show();
    m_imgLabel->hide();
    m_videoWidget->hide();
    m_audioLabel->hide();
    m_textEdit->hide();
    m_controlBar->hide();
    m_imgSpace->hide();
}

void PreviewPanel::showText(const QString& path) {
    m_mode = "text";
    if (m_player) m_player->stop();
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    m_audioLabel->hide();
    m_controlBar->hide();
    if (m_liveBadge) m_liveBadge->hide();

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 大文件截断(预览面板不是编辑器,512KB 足够看开头)
        const qint64 maxSize = 512 * 1024;
        QByteArray data = f.read(maxSize);
        f.close();
        m_textEdit->setPlainText(QString::fromUtf8(data));
        m_textEdit->show();
    } else {
        m_textEdit->setPlainText(QString::fromUtf8("无法读取文件"));
        m_textEdit->show();
    }
}

void PreviewPanel::showText(const QString& path) {
    m_mode = "text";
    if (m_player) m_player->stop();
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    m_audioLabel->hide();
    m_controlBar->hide();
    if (m_liveBadge) m_liveBadge->hide();

    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // 大文件截断(预览面板不是编辑器,512KB 足够看开头)
        const qint64 maxSize = 512 * 1024;
        QByteArray data = f.read(maxSize);
        f.close();
        m_textEdit->setPlainText(QString::fromUtf8(data));
        m_textEdit->show();
    } else {
        m_textEdit->setPlainText(QString::fromUtf8("无法读取文件"));
        m_textEdit->show();
    }
}

void PreviewPanel::setupPlayer() {
    if (m_player) return;

    // Qt6 API: QMediaPlayer + QAudioOutput
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8);

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
        m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(
            state == QMediaPlayer::PlayingState
                ? QStyle::SP_MediaPause : QStyle::SP_MediaPlay)));
        if (state == QMediaPlayer::PlayingState && m_videoCover)
            m_videoCover->hide();   // 真正开播才露出画面,残帧永远无曝光窗口
    });

    // Live Photo: 视频播完 → 切回静态图(播放器保留复用)。
    // 连接无条件建立(首次 setupPlayer 时 m_isLivePhoto 未必为 true),
    // 槽内以 m_isLivePhoto 判断,避免普通视频播完误触发切图
    connect(m_player, &QMediaPlayer::mediaStatusChanged,
            this, [this](QMediaPlayer::MediaStatus status) {
        // teardownPlayer 断连后不会到此;但快速切换文件时旧信号可能迟到送达
        if (!m_player) return;
        // 媒体后端的加载/失效/卡住都会从这里过——卡死排查的关键轨迹
        if (m_mode == "video" || m_isLivePhoto)
            Logger::event(QStringLiteral("mediaStatus=%1 src='%2'")
                              .arg(int(status))
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
                m_vw->show();
                m_player->setVideoOutput(m_vw);
                m_videoOutAttached = true;
                if (m_isLivePhoto || (m_mode == "video" && pp_impl::s_bool("Viewer/autoPlayVideo", true))) {
                    m_player->play();
                    m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(QStyle::SP_MediaPause)));
                } else {
                    m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(QStyle::SP_MediaPlay)));
                }
            }
        }
        if (status == QMediaPlayer::InvalidMedia) {
            m_pendingPlay.clear();
            if (m_videoCover) m_videoCover->hide();   // 源失效:收遮罩,露出空态而非永久黑屏
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

    // 播放/暂停按钮
    connect(m_btnPlay, &QPushButton::clicked, this, [this]() {
        if (!m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState)
            m_player->pause();
        else
            m_player->play();
    });

    // 进度条拖动
    connect(m_progress, &QSlider::sliderMoved, this, [this](int pos) {
        if (m_player) m_player->setPosition(pos);
    });

    // 时长变化
    connect(m_player, &QMediaPlayer::durationChanged, this, [this](qint64 dur) {
        m_progress->setRange(0, static_cast<int>(dur));
    });

    // 位置更新（Qt6 signal，替换 Qt5 timer）
    connect(m_player, &QMediaPlayer::positionChanged, this, [this](qint64 pos) {
        qint64 dur = m_player->duration();
        if (dur > 0) {
            m_progress->setValue(static_cast<int>(pos));
        }
        auto fmt = [](qint64 ms) -> QString {
            int sec = static_cast<int>(ms / 1000);
            int h = sec / 3600;
            return h > 0
                ? QString("%1:%2:%3").arg(h).arg((sec % 3600) / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'))
                : QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
        };
        qint64 shown = m_timeRemaining ? (dur - pos) : pos;
        m_timeLabel->setText(fmt(shown) + " / " + fmt(dur));
    });
}

void PreviewPanel::teardownPlayer() {
    if (!m_player) return;
    Logger::event(QStringLiteral("teardownPlayer src='%1'").arg(m_player->source().toLocalFile()));
    Logger::event(QStringLiteral("teardownPlayer src='%1'").arg(m_player->source().toLocalFile()));
    // 立即断开视频输出:阻止播放器继续往 QVideoWidget 渲染帧
    if (m_vw) {
        m_player->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
        m_videoOutAttached = false;
    }
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
    m_audioLabel->hide();
    m_controlBar->hide();
    m_placeholder->show();
    if (m_liveBadge) m_liveBadge->hide();
    m_mode = "none";
    m_scale = 1.0;
    m_livePhotoOriginalPath.clear();
    m_isLivePhoto = false;
}
