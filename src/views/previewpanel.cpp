#include "previewpanel.h"
#include "livephoto.h"
#include "wicdecode.h"
#include "settings.h"
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
#include <QDesktopServices>
#include <QMimeData>
#include <QMediaDevices>
#include <QWidgetAction>
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
#include <QStyle>
#include <QWidgetAction>
#include <QMenu>
#include <QVideoFrame>
#include <QVideoSink>
#include "previewpanel_internal.h"

// (VIDFRAME 帧旁听探针已于 2026-09-01 剪除:它完成了 #101 AV1 黑屏、#104 闪回、
//  #121 HDR 三轮诊断 —— 结论都写进了 TODO_ALL。播放侧的常驻通道留 hb/mediaStatus。)

PreviewPanel::PreviewPanel(QWidget* parent) : QWidget(parent) {
    setStyleSheet(QString("background:%1;").arg(C_PREVIEW_BG));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // 空态占位(未选中任何文件时)
    m_placeholder = new QLabel("\xe9\xa2\x84\xe8\xa7\x88\xe5\x8c\xba"); // 预览区
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
    installEventFilter(this);

    // 音量按钮:弹出竖向音量滑条
    connect(m_btnVolume, &QToolButton::clicked, this, [this]() {
        if (!m_audioOutput) return;
        QMenu volMenu(this);
        auto* slider = new QSlider(Qt::Vertical, &volMenu);
        slider->setRange(0, 100);
        slider->setValue(static_cast<int>(m_audioOutput->volume() * 100));
        slider->setFixedSize(28, 110);
        connect(slider, &QSlider::valueChanged, this, [this](int v) {
            if (m_audioOutput) m_audioOutput->setVolume(v / 100.0);
        });
        auto* act = new QWidgetAction(&volMenu);
        act->setDefaultWidget(slider);
        volMenu.addAction(act);
        volMenu.exec(m_btnVolume->mapToGlobal(
            QPoint(m_btnVolume->width() / 2 - 60, -120)));
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
    auto scrollTo = [this](int, int) {
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
            [this]() { if (m_origPix) { m_scale = 1.0; m_ctrlZoomed = true; render(); } });
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

void PreviewPanel::setViewerMode(bool on) {
    if (m_viewerMode == on) return;
    m_viewerMode = on;
    applyBackdrop();
    applyViewerChrome();
}

void PreviewPanel::loadFile(const QString& path) {
    if (path.isEmpty()) { clear(); return; }
    QFileInfo fi(path);
    if (!fi.exists()) { clear(); return; }

    m_filePath = path;
    m_livePhotoOriginalPath.clear();
    m_isLivePhoto = false;

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
            QString videoPath;
            if (liveInfo->embedded && liveInfo->videoOffset >= 0) {
                // 内嵌型：优先复用本会话已提取的临时文件，避免反复 remux + %TEMP% 堆积
                if (m_extractCache.contains(path)) {
                    videoPath = m_extractCache.value(path);
                } else {
                    videoPath = LivePhoto::extractEmbeddedVideo(path, *liveInfo);
                    if (!videoPath.isEmpty())
                        m_extractCache.insert(path, videoPath);
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
        m_placeholder->show();
        m_imgLabel->hide();
        m_videoWidget->hide();
        m_audioLabel->hide();
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

void PreviewPanel::setupPlayer() {
    if (m_player) return;

    // Qt6 API: QMediaPlayer + QAudioOutput
    m_player = new QMediaPlayer(this);
    m_audioOutput = new QAudioOutput(this);
    m_player->setAudioOutput(m_audioOutput);
    m_audioOutput->setVolume(0.8);

    if (m_mode == "video") {
        // 清理上一个视频的 QVideoWidget:不删则旧帧残留,
        // 切换视频瞬间闪回上一文件一帧(新帧解码完成前旧 vw 透出)+ widget 堆积泄漏
        for (auto* child : m_videoWidget->children()) {
            if (auto* w = qobject_cast<QWidget*>(child)) {
                if (w == m_liveBadge) continue;   // 保留 LIVE 徽章
                w->hide();
                w->deleteLater();
            }
        }
        auto* vw = new QVideoWidget(m_videoWidget);
        vw->setGeometry(m_videoWidget->rect());
        vw->show();
        m_player->setVideoOutput(vw);
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
        // WMF 后端的加载/失效/卡住都会从这里过——卡死排查的关键轨迹
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
        if (status == QMediaPlayer::InvalidMedia) m_pendingPlay.clear();
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
    // 立即断开视频输出:阻止旧播放器继续往 QVideoWidget 渲染帧。
    // 否则 stop() 后最后一帧残留 + deleteLater 窗口期内旧帧持续透出(切视频闪回)
    if (m_mode == "video")
        m_player->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
    m_player->stop();
    // deleteLater：对象在事件循环末尾销毁，避免同步 delete 的 UAF 风险
    m_player->deleteLater();
    m_player = nullptr;
    if (m_audioOutput) {
        m_audioOutput->deleteLater();
        m_audioOutput = nullptr;
    }
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
