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
#include <QStandardPaths>
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

// 去封面副本缓存查询(定义在 startAudioRemux 前;showAudio 先用,前置声明)
static QString remuxedCopyFor(const QString& src);

// Viewer/autoPlayAudioCompanion:图片旁存在同名音频时自动播放
void PreviewPanel::playAudioCompanion(const QString& imagePath) {
    if (!pp_impl::s_bool("Viewer/autoPlayAudioCompanion", false)) return;
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

// Live Photo 内嵌视频后台提取:完成且回到该文件时自动切播放
void PreviewPanel::startExtractAsync(const QString& path, const LivePhoto::Info& info) {
    if (m_extractBusy) { Logger::event("extract: busy, skip"); return; }
    m_extractBusy = true;
    Logger::event(QStringLiteral("extract: submit '%1' off=%2 len=%3")
                      .arg(path).arg(info.videoOffset).arg(info.videoLength));
    // Panel 被销毁时 worker 回调若访问 this → UAF;用 QPointer 阻断
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, path, info]() {
        QElapsedTimer sw;
        sw.start();
        const QString vp = LivePhoto::extractEmbeddedVideo(path, info);
        const qint64 ms = sw.elapsed();
        QMetaObject::invokeMethod(self, [self, path, vp, ms]() {
            if (!self) return;  // panel was destroyed
            self->m_extractBusy = false;
            Logger::event(QStringLiteral("extract: done %1 ms '%2'")
                              .arg(ms).arg(vp.isEmpty() ? QStringLiteral("<failed>") : vp));
            if (vp.isEmpty()) return;
            self->m_extractCache.insert(path, vp);
            // 只入缓存,不自动播放——播放仅由单击预览窗格触发(用户定版)
        }, Qt::QueuedConnection);
    });
}

// 动态照片单击播放:提取缓存直接播;缓存未就绪则后台提取,再点即播
void PreviewPanel::playLivePhoto() {
    if (!m_liveInfo) return;
    Logger::event(QStringLiteral("playLivePhoto '%1'").arg(m_filePath));
    QString vp;
    if (m_liveInfo->embedded) {
        if (m_liveInfo->videoOffset < 0) return;   // 只有 XMP 无偏移:无从提取
        vp = m_extractCache.value(m_filePath);
        if (vp.isEmpty()) { Logger::event("replay: cache miss, extract"); startExtractAsync(m_filePath, *m_liveInfo); return; }
    } else {
        vp = m_liveInfo->videoPath;
    }
    if (vp.isEmpty() || !QFileInfo::exists(vp)) { Logger::event("replay: video missing"); return; }
    m_livePhotoOriginalPath = m_filePath;
    m_isLivePhoto = true;
    showVideo(vp);
}

// 进度条按下/拖动共用的擦洗落点:GIF 走逐帧轴(单位 ms),视频/音频走播放器。
// 拖出条外的 x 由 ratio 钳到 0..1;无轴可擦(GIF 解析失败/无时长)时不动。
// 句柄/填充必须在这里直接 setValue:回放侧的写入在拖动期被 isSliderDown
// 拦截(见 gifSyncToFrame/positionChanged),拖动中滑块只听擦洗的。
void PreviewPanel::progressScrub(qreal x)
{
    if (m_progress->width() <= 0) return;
    const double ratio = qBound(0.0, double(x) / m_progress->width(), 1.0);
    if (m_isGif) {
        if (m_progress->maximum() > 0) {
            const int ms = int(ratio * m_progress->maximum());
            m_progress->setValue(ms);
            gifSeekMs(ms);
        }
        return;
    }
    if (m_player && m_player->duration() > 0) {
        const qint64 pos = static_cast<qint64>(ratio * m_player->duration());
        m_progress->setValue(static_cast<int>(pos));
        m_player->setPosition(pos);
        // 播完后拖进度条:seek 到位即接续播放(黑屏不等)
        if (m_player->playbackState() != QMediaPlayer::PlayingState)
            m_player->play();
    }
}

void PreviewPanel::showVideo(const QString& path) {
    m_mode = "video";
    m_placeholder->hide();
    m_imgLabel->hide();
    setAudioChrome(false);
    m_textEdit->hide();
    m_videoWidget->show();

    // Live Photo：自动播放的几秒短片，无需控制栏，仅显示 LIVE 徽章
    const bool live = m_isLivePhoto;
    const bool playbar = !inFullscreen() || pp_impl::s_bool("Fullscreen/showPlaybar", true);
    m_btnVolume->show();   // GIF(#97)会藏掉音量键,回到视频必须还原
    m_controlBar->setVisible(!live && playbar);
    if (!live && playbar) updateGFullPlaybar(nullptr);   // G 全屏视频:进场先藏,光标到底部才出(2026-09-08)
    m_imgSpace->hide();
    syncVideoChildren();   // videoWidget 刚 show,布局尚未激活,先把当下矩形铺上
    updateLiveBadge();     // 播放 live 视频时徽章照常亮在面板右上角(与静态态同位)

    setupPlayer();
    if (!m_player) {
        // 媒体栈冷启动门闩挂起了本次装载(FFmpeg DLL 未预热,1~3 秒后 replay)。
        // 此时绝不能把视频面留着:它露的是上一路的末帧,用户看到的就是
        // "双击视频却打开了别的文件"(2026-09-09 用户报)。藏起来 + 升遮罩,
        // 屏幕上只剩父窗口底色,replay 后正常走首帧 reveal。
        raiseVideoCover();
        QTimer::singleShot(0, this, [this]() { syncVideoChildren(); });
        return;
    }
    if (m_player) {
        ensureVideoWidget();          // 复用同一 QVideoWidget(切视频不重建)
        applyVideoBackdrop();         // 底色按当前模式/主题取(全屏恒黑,浅色浏览器=白)
        const QUrl url = QUrl::fromLocalFile(path);
        if (m_player->source() == url) {
            // 同一媒体(连点动态照片重播):直接回零重播。反复走下面那套
            // "断开输出→setSource→重连"会让媒体后端快速装卸,连点即拖死后端
            Logger::event(QStringLiteral("showVideo: same src, rewind '%1'").arg(path));
            // switch 刚落下(pendingPlay 未消费)时重复 loadFile(启动恢复双触发)
            // 会走到这:媒体还在装载,此刻清票+抢接输出实测让 FFmpeg 后端视频
            // 管线断供——有声、位置照走、一帧都到不了 sink(VIDFRAME 探针实锤)。
            // 只盖遮罩防残帧,attach+play 交给 LoadedMedia 的延迟 attach 统一处理
            const bool deferLoad = !m_pendingPlay.isEmpty();
            if (deferLoad) {
                Logger::event(QStringLiteral("showVideo: same src while loading, defer"));
            } else {
                m_pendingPlay.clear();
            }
            if (!deferLoad && !m_videoOutAttached) {
                // 上一次 switch 断开了输出而延迟 attach 被旧源的迟到事件破坏:
                // 不接回输出就 play 会"只出声不出画"
                Logger::event(QStringLiteral("showVideo: same src, re-attach output"));
                m_player->setVideoOutput(m_vw);
                m_videoOutAttached = true;
            }
            // #104:raiseVideoCover() 会把 m_vw 藏起来(遮罩盖不住原生视频窗,
            // 只能连控件一起藏),本路首帧到达时由 revealVideo() 放出来。
            // 回零重播前的"上一轮末帧"因此完全没有曝光窗口。
            raiseVideoCover();
            armCoverUntilFirstFrame();
            if (!deferLoad) {
                m_player->setPosition(0);
                if (live || pp_impl::s_bool("Viewer/autoPlayVideo", true)) {
                    m_player->play();
                    m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPause)));
                } else {
                    m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPlay)));
                }
            }
        } else {
            Logger::event(QStringLiteral("showVideo: switch '%1' (was '%2', deferred attach)")
                              .arg(path).arg(m_player->source().toLocalFile()));
            // 延迟 attach 防线:setSource 后不在装载期抢接输出——等 mediaStatus
            // (LoadedMedia/BufferedMedia)就绪后由槽里 attach + play。
            // ("textureConverter null" 警告实测为 FFmpeg 后端首帧预热丢帧,非崩溃)
            m_pendingPlay = path;
            m_player->stop();
            m_player->setVideoOutput(static_cast<QVideoWidget*>(nullptr));
            m_videoOutAttached = false;
            m_videoSize = QSize();   // 新源:等首帧重记画面尺寸
            // #104:装载期把视频控件整个藏起来(遮罩盖不住它内部的原生视频窗)。
            // 实测断输出后视频面还会继续呈现上一路的末帧约 50~100ms——那就是
            // 用户看到的"闪回上一张"。藏起来则屏幕上只剩父窗口的 #0A0A0C 深色底。
            raiseVideoCover();
            m_player->setSource(url);
        }
    }

    // 布局激活是异步的:上面拿到的可能还是 GIF 形态改写出的半高残影,
    // 激活完成后再同步一次(0ms 定时器的事件排在已入队的 LayoutRequest 之后)
    QTimer::singleShot(0, this, [this]() {
        syncVideoChildren();
    });
}

// VIDFRAME 探针(旧 vidProbeAttach)已随 #121 收尾剪除,见文件顶部说明。

// 视频面/遮罩底色唯一写入口:未播放/切源期间露出的底 = backdropColor()
// (浏览器浅色主题默认白,深色与全屏恒黑)。主题切换与创建时都走这里,
// 不再各处硬编码 #000000(白色主题下视频区一片黑的根因)。
void PreviewPanel::applyVideoBackdrop() {
    const QPalette pal(backdropColor());
    if (m_vw) m_vw->setPalette(pal);
    if (m_videoCover) m_videoCover->setPalette(pal);
}

// 视频控件唯一创建/复用入口:切视频时复用同一实例(标准 setSource 切换),
// Qt 内部清空视频输出;无帧窗口期显示纯黑,绝不透出下层残留画面
void PreviewPanel::ensureVideoWidget() {
    if (m_vw) return;
    QElapsedTimer initSw;
    initSw.start();
    m_vw = new QVideoWidget(m_videoWidget);
    m_vw->setGeometry(m_videoWidget->rect());
    m_vw->setAutoFillBackground(true);
    m_vw->setPalette(QPalette(backdropColor()));   // 浅色主题=白底(随主题/全屏恒黑)
    m_vw->setMouseTracking(true);   // #208:视频面悬停也要把 move 冒泡给面板(光标恢复/信息条)
    m_vw->installEventFilter(this);   // 它盖满 m_videoWidget,点击先到它
    m_vw->show();
    if (!m_videoCover) {
        // 遮罩是 m_videoWidget 的子件(随 resizeEvent 自动重设几何),
        // 底色随主题 + 盖在 m_vw 之上;showVideo 时升起,Playing 后收回。
        // 必须带 objectName:裸 QWidget 会被全局 QWidget{background:%2} 命中,
        // QSS 背景压过调色板 → 缓冲瞬间遮罩被涂成 #212126(33,33,38),
        // #pvVideoCover 规则(theme.cpp,同 %40 令牌)精确接管底色调。
        m_videoCover = new QWidget(m_videoWidget);
        m_videoCover->setObjectName("pvVideoCover");
        m_videoCover->setAutoFillBackground(true);
        m_videoCover->setPalette(QPalette(backdropColor()));
    }
    m_videoCover->setGeometry(m_videoWidget->rect());
    // QVideoWidget 首建要拉起视频渲染管线(D3D 设备/swapchain),与 QMediaPlayer
    // 构造同属"媒体栈冷启动",耗时同埋探针便于归因
    Logger::event(QStringLiteral("ensureVideoWidget: QVideoWidget %1 ms")
                      .arg(initSw.elapsed()));
}

// 预热媒体栈(2026-09-05 重做):QMediaPlayer/QVideoWidget 的首次创建同步且重
// (日志实测 2~4 秒),大头是 FFmpeg 后端 DLL(66MB avcodec)的首载——LoadLibrary
// 是进程级的,在**任何**线程建一次播放器,DLL 就驻留全程。所以这笔账改在
// 全局池线程付:临时实例走完一生即毁,GUI 线程从此不在启动期被冻结
// (过去同步预热把主线程钉住 2.2 秒,文件页定格在半成品帧上——用户报的
// "启动灰块卡几秒")。真需要 player 时(点视频/恢复视频)setupPlayer 现场
// 建,只剩管线初始化。重复调用无害。
void PreviewPanel::warmUp() {
    static std::atomic<bool> dllWarmed{false};
    if (!dllWarmed.exchange(true)) {
        QPointer<PreviewPanel> self(this);
        QThreadPool::globalInstance()->start([self]() {
            QMediaPlayer probe;          // 无 parent:建、毁都在本池线程
            QAudioOutput out;
            probe.setAudioOutput(&out);
            // 探针走完,DLL 已驻留进程。置就绪门闩并回 GUI 线程兑现:
            // ①建本面板视频控件(0ms,DLL 已在);②重放门闩期间挂起的装载。
            // 挂在 qApp 上而非面板——就算面板此刻已销毁,门闩也得照常置位,
            // 否则后续所有视频装载会永远挂起(挂起无人兑现的死循环)。
            QMetaObject::invokeMethod(qApp, [self]() {
                pp_impl::mediaReadyFlag().store(true, std::memory_order_release);
                if (self) self->ensureVideoWidget();
                for (const auto& r : pp_impl::takePendingMediaLoads()) {
                    auto* p = qobject_cast<PreviewPanel*>(r.panel.data());
                    if (p && !r.path.isEmpty()) {
                        Logger::event(QStringLiteral(
                            "media gate: replay '%1'").arg(r.path));
                        p->loadFile(r.path);
                    }
                }
            }, Qt::QueuedConnection);
        });
    }
}

void PreviewPanel::showAudio(const QString& path) {
    m_mode = "audio";
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    m_textEdit->hide();
    m_controlBar->show();
    m_imgSpace->hide();
    m_btnVolume->show();   // GIF(#97)会藏掉音量键,回到音频必须还原

    QFileInfo fi(path);
    m_audioLabel->setText(fi.fileName());
    setAudioChrome(true);

    setupPlayer();
    if (m_player) {
        // 去封面副本命中 = 直接当源(2026-09-06 用户令"完全忽视封面秒放"):
        // 不再先载原文件等自检再换,第二次起播放零等待
        const QString ready = remuxedCopyFor(path);
        m_player->setSource(QUrl::fromLocalFile(ready.isEmpty() ? path : ready));
        m_player->play();
        m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPause)));
    }

    // 波形启动:解码聚合全在专属线程,主线程在这里只花微秒级(性能红线:
    // 波形晚出可以,切文件/加载音频的性能一毫秒不能被它吃掉)。先清快照
    // 画中线占位,快照到货渐进成形。
    ensureWave();
    m_waveSnap = Audiowave::Snapshot{};
    renderWave();
    if (m_waveWorker)
        QMetaObject::invokeMethod(m_waveWorker, "start",
                                  Q_ARG(QString, path), Q_ARG(qint64, 0));
    // 布局激活是异步的:0 尺寸画的占位要在激活后重画一次
    QTimer::singleShot(0, this, [this]() { renderWave(); });

    // 内嵌封面流早发现(2026-09-06 用户报某 mp3 首播等好几秒):带封面流的
    // 音频会让后端装载慢/位置冻结,靠 3 秒自检兜底体感就是"等好几秒"。
    // 装载同时后台 ffprobe(~0.2s)主动确认,带视频流就直接换重封装副本
    //(副本按内容指纹缓存,二次播放秒开);仅 position 未起跳时才换,不打断播放
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, path]() {
        const QString ff = locateFfmpegTool(QStringLiteral("ffprobe"));
        if (ff.isEmpty()) return;
        QProcess p;
        hideConsoleWindow(p);
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(ff, { QStringLiteral("-v"), QStringLiteral("error"),
                      QStringLiteral("-show_entries"), QStringLiteral("stream=codec_type"),
                      QStringLiteral("-of"), QStringLiteral("csv"), path });
        if (!p.waitForFinished(2500)) return;
        const QString outp = QString::fromLocal8Bit(p.readAllStandardOutput());
        if (!outp.contains(QLatin1String("video"))) return;
        QMetaObject::invokeMethod(self, [self, path]() {
            if (!self || self->m_mode != "audio" || self->m_filePath != path) return;
            if (!self->m_player) return;
            if (self->m_player->source() != QUrl::fromLocalFile(path)) return;
            if (self->m_player->position() > 200) return;   // 已经在正常播:不打断
            self->startAudioRemux();
        }, Qt::QueuedConnection);
    });

    // 播放卡死自检兜底:带内嵌封面流(mjpeg attached_pic,网易云下载件常态)的音频
    // 会让 FFmpeg 后端装载链全正常(BufferedMedia)但播放位置永远冻结在 0。
    // 1.5 秒后仍在 Playing 且 pos==0 → 判卡死,后台重封装去封面副本换源重放
    QTimer::singleShot(1500, this, [this, path] {
        if (m_mode != "audio" || m_filePath != path || !m_player) return;
        if (m_player->playbackState() == QMediaPlayer::PlayingState
            && m_player->position() <= 0)
            startAudioRemux();
    });
}

// ── 音频播放卡死自救:重封装去封面副本换源重放 ──
// 探针 2026-09-04 实测(合成带封面 MP3 vs 同件剥封面):前者 pos=0 恒定,
// 后者位置正常前进 —— 病根是内嵌封面流,不是文件本身。用随包 ffmpeg 以
// stream copy(-map 0:a -c:a copy,不重编码,秒级)产只含音频流的副本。
// 副本落临时目录按内容指纹命名,跨会话复用;失败/切走即弃,诚实降级。
// 去封面副本的缓存查询:命中(非空文件存在)返回路径,没建过返回空串。
// showAudio 用它在 setSource 之前就换上副本——第二次播放起真·秒放
static QString remuxedCopyFor(const QString& src) {
    const QFileInfo fi(src);
    const QString ext = fi.suffix().toLower();
    const QString key = QString::number(qHash(src) ^ qHash(fi.size())
        ^ qHash(fi.lastModified().toMSecsSinceEpoch()), 16);
    const QString outDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + QStringLiteral("/gaze-audio-remux");
    const QString out = outDir + QStringLiteral("/%1.%2")
                            .arg(key, ext.isEmpty() ? QStringLiteral("mp3") : ext);
    return QFileInfo(out).size() > 0 ? out : QString();
}

void PreviewPanel::startAudioRemux() {
    if (m_remuxProc) return;   // 一单在途
    const QString src = m_filePath;
    if (src.isEmpty() || !m_player) return;
    if (m_player->source() != QUrl::fromLocalFile(src)) return;   // 已换过源(重入闸)
    const QString exe = locateFfmpegTool(QStringLiteral("ffmpeg"));
    if (exe.isEmpty()) {
        Logger::event("audio remux: ffmpeg 不可用(exe旁 ffmpeg/ 与 PATH 均无),放弃换源");
        return;
    }
    QDir().mkpath(QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                  + QStringLiteral("/gaze-audio-remux"));
    const QString cached = remuxedCopyFor(src);
    if (!cached.isEmpty()) {   // 旧副本仍在:直接换源,不再重封装
        Logger::event(QStringLiteral("audio remux: 复用副本 %1").arg(cached));
        swapAudioSource(cached);
        return;
    }
    const QFileInfo fi(src);
    const QString ext = fi.suffix().toLower();
    const QString key = QString::number(qHash(src) ^ qHash(fi.size())
        ^ qHash(fi.lastModified().toMSecsSinceEpoch()), 16);
    const QString outDir = QStandardPaths::writableLocation(QStandardPaths::TempLocation)
                           + QStringLiteral("/gaze-audio-remux");
    const QString out = outDir + QStringLiteral("/%1.%2")
                            .arg(key, ext.isEmpty() ? QStringLiteral("mp3") : ext);
    Logger::event(QStringLiteral(
        "audio remux: 播放卡死(疑似内嵌封面流)→ 重封装 '%1'").arg(src));
    m_remuxProc = new QProcess(this);
    hideConsoleWindow(*m_remuxProc);   // ffmpeg 是控制台程序,不藏就闪黑窗
    connect(m_remuxProc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, [this, src, out](int code, QProcess::ExitStatus) {
        QString errText;
        if (m_remuxProc) errText = QString::fromLocal8Bit(m_remuxProc->readAllStandardError());
        QProcess* p = m_remuxProc;
        m_remuxProc = nullptr;
        if (p) p->deleteLater();
        if (code != 0 || QFileInfo(out).size() <= 0) {
            Logger::event(QStringLiteral("audio remux: 失败 code=%1 %2,维持原样")
                              .arg(code).arg(errText.left(200)));
            return;
        }
        if (m_mode != "audio" || m_filePath != src) return;   // 用户已切走:只留副本下次复用
        swapAudioSource(out);
    });
    m_remuxProc->start(exe, { QStringLiteral("-v"), QStringLiteral("error"),
                              QStringLiteral("-i"), src,
                              QStringLiteral("-map"), QStringLiteral("0:a"),
                              QStringLiteral("-c:a"), QStringLiteral("copy"),
                              QStringLiteral("-y"), out });
}

void PreviewPanel::swapAudioSource(const QString& out) {
    if (!m_player) return;
    Logger::event(QStringLiteral("audio remux: 换源重放 '%1'").arg(out));
    m_player->setSource(QUrl::fromLocalFile(out));
    m_player->play();
    if (m_btnPlay)
        m_btnPlay->setIcon(pp_impl::themeIcon(style()->standardIcon(QStyle::SP_MediaPause)));
}

// ── 音频波形(见 audiowave.h;解码聚合全在专属线程,这里只画) ──

void PreviewPanel::ensureWave() {
    if (m_waveThread) return;
    qRegisterMetaType<Audiowave::Snapshot>("Audiowave::Snapshot");
    m_waveWorker = new Audiowave::Worker;
    m_waveThread = new QThread(this);
    m_waveThread->setObjectName(QStringLiteral("audiowave"));
    connect(m_waveThread, &QThread::finished, m_waveWorker, &QObject::deleteLater);
    connect(m_waveWorker, &Audiowave::Worker::snapshotReady,
            this, &PreviewPanel::onWaveSnapshot);   // 跨线程 → 自动 queued
    m_waveWorker->moveToThread(m_waveThread);
    m_waveThread->start(QThread::LowPriority);   // 低优先级:解码让路于 UI/播放
}

void PreviewPanel::teardownWave() {
    if (!m_waveThread) return;
    if (m_waveWorker)
        QMetaObject::invokeMethod(m_waveWorker, "cancel");
    m_waveThread->quit();
    m_waveThread->wait(2000);
    m_waveThread = nullptr;
    m_waveWorker = nullptr;   // finished→deleteLater 已随线程收尾执行
}

void PreviewPanel::onWaveSnapshot(Audiowave::Snapshot snap) {
    if (m_mode != "audio") return;   // worker 代次闸之后的双保险:切走的文件不画
    m_waveSnap = std::move(snap);
    renderWave();
}

// 音频形态两件套(文件名+波形)统一显隐:此前 7 处散布 m_audioLabel->hide(),
// 波形加入后散改必漏一处 → 收口到这一个出口(showAudio true,其余全 false)
void PreviewPanel::setAudioChrome(bool on) {
    // RAW 占位(#140)与其他形态互斥:所有形态切换都会经过这里(各 showX 已统一
    // 调 setAudioChrome),raw 占位借道一并收起;showRawPlaceholder 在调用之后
    // 才 show,次序保证 raw 形态自己不受影响
    if (m_rawBox) m_rawBox->hide();
    m_audioLabel->setVisible(on);
    if (m_waveLabel) m_waveLabel->setVisible(on);
}

void PreviewPanel::renderWave() {
    if (!m_waveLabel || !m_waveLabel->isVisible()) return;
    if (m_waveSnap.failed) {
        m_waveLabel->setText(
            gazeTr("波形不可用")); // 波形不可用
        return;
    }
    const int w = qMax(64, m_waveLabel->width());
    const int h = qMax(32, m_waveLabel->height());
    QPixmap pm(w, h);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    const QColor sep(QString(C_SEPARATOR)), acc(QString(C_ACCENT));
    const int mid = h / 2;
    const int n = qMin(m_waveSnap.filled, Audiowave::kBuckets);
    if (n <= 0) {
        p.fillRect(0, mid, w, 1, sep);   // 无数据:中线占位,等首批快照
        p.end();
        m_waveLabel->setPixmap(pm);
        return;
    }
    const qreal bw = qreal(w) / Audiowave::kBuckets;
    const int half = qMax(2, mid - 2);
    for (int b = 0; b < n; ++b) {
        const int x = int(b * bw);
        const int wide = qMax(1, int(bw) - 1);
        const int pk = m_waveSnap.peak[b], tr = m_waveSnap.trough[b];
        if (pk == 0 && tr == 0) {   // 静音桶:中线上一个短点
            p.fillRect(x, mid, wide, 1, sep);
            continue;
        }
        const int top = mid - pk * half / 127;
        const int bot = mid - tr * half / 127;
        p.fillRect(x, top, wide, qMax(1, bot - top), acc);
    }
    p.end();
    m_waveLabel->setPixmap(pm);
}

void PreviewPanel::finishLivePhoto() {
    if (!m_player || !m_isLivePhoto) return;

    // 复用架构:Live Photo 播完不销毁播放器/视频控件(销毁重建会闪),
    // 仅停止播放并隐藏视频区切回静态图;下次播放复用同一实例
    Logger::event("finishLivePhoto: back to static");
    m_player->stop();
    m_videoWidget->hide();

    QString orig = m_livePhotoOriginalPath;
    m_livePhotoOriginalPath.clear();
    m_isLivePhoto = false;
    if (!orig.isEmpty())
        showImage(orig);   // showImage 内部会隐藏徽章与控制栏
}

void PreviewPanel::cleanupExtractCache() {
    Logger::event(QStringLiteral("cleanupExtractCache: %1 temp file(s)").arg(m_extractCache.size()));
    for (auto it = m_extractCache.constBegin(); it != m_extractCache.constEnd(); ++it)
        QFile::remove(it.value());
    m_extractCache.clear();
}
