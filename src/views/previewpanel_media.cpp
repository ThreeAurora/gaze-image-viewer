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
    }
}

void PreviewPanel::showVideo(const QString& path) {
    m_mode = "video";
    m_placeholder->hide();
    m_imgLabel->hide();
    m_audioLabel->hide();
    m_textEdit->hide();
    m_videoWidget->show();

    // Live Photo：自动播放的几秒短片，无需控制栏，仅显示 LIVE 徽章
    const bool live = m_isLivePhoto;
    const bool playbar = !inFullscreen() || pp_impl::s_bool("Fullscreen/showPlaybar", true);
    m_btnVolume->show();   // GIF(#97)会藏掉音量键,回到视频必须还原
    m_controlBar->setVisible(!live && playbar);
    m_imgSpace->hide();
    syncVideoChildren();   // videoWidget 刚 show,布局尚未激活,先把当下矩形铺上
    if (m_liveBadge) {
        m_liveBadge->raise();
        m_liveBadge->adjustSize();
        m_liveBadge->move(m_videoWidget->width() - m_liveBadge->width() - 12, 12);
        m_liveBadge->setVisible(live);
    }

    setupPlayer();
    if (m_player) {
        ensureVideoWidget();          // 复用同一 QVideoWidget(切视频不重建)
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
            m_vw->show();
            raiseVideoCover();   // 回零重播前盖住上一轮的末帧,本路首帧到达后收回
            armCoverUntilFirstFrame();
            if (!deferLoad) {
                m_player->setPosition(0);
                if (live || pp_impl::s_bool("Viewer/autoPlayVideo", true)) {
                    m_player->play();
                    m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(QStyle::SP_MediaPause)));
                } else {
                    m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(QStyle::SP_MediaPlay)));
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
            // 装载期用纯黑遮罩盖住视频控件:控件里残留的是上一段视频的最后一帧,
            // attach→新视频首帧之间会"闪回上一张";遮罩露出的是纯黑底
            m_vw->show();
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

// 视频控件唯一创建/复用入口:切视频时复用同一实例(标准 setSource 切换),
// Qt 内部清空视频输出;无帧窗口期显示纯黑,绝不透出下层残留画面
void PreviewPanel::ensureVideoWidget() {
    if (m_vw) return;
    m_vw = new QVideoWidget(m_videoWidget);
    m_vw->setGeometry(m_videoWidget->rect());
    m_vw->setAutoFillBackground(true);
    m_vw->setPalette(QPalette(QColor("#000000")));
    m_vw->installEventFilter(this);   // 它盖满 m_videoWidget,点击先到它
    m_vw->show();
    if (!m_videoCover) {
        // 遮罩是 m_videoWidget 的子件(随 resizeEvent 自动重设几何),
        // 显式置黑 + 盖在 m_vw 之上;showVideo 时升起,Playing 后收回
        m_videoCover = new QWidget(m_videoWidget);
        m_videoCover->setAutoFillBackground(true);
        m_videoCover->setPalette(QPalette(QColor("#000000")));
    }
    m_videoCover->setGeometry(m_videoWidget->rect());
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
    m_audioLabel->show();

    setupPlayer();
    if (m_player) {
        m_player->setSource(QUrl::fromLocalFile(path));
        m_player->play();
        m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(QStyle::SP_MediaPause)));
    }
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
