#pragma once
#include <QWidget>
#include <QLabel>
#include <QSlider>
#include <QPushButton>
#include <QToolButton>
#include <QMediaPlayer>
#include <QAudioOutput>
#include <QVideoWidget>
#include <QMap>
#include <QTimer>

class QProcess;
#include <QTimer>


class PreviewPanel : public QWidget {
    Q_OBJECT
public:
    explicit PreviewPanel(QWidget *parent = nullptr);
    ~PreviewPanel() override;
    void loadFile(const QString &path);
    void clear();
    void togglePlayPause();
    void seekDelta(int seconds);

    int scalePercent() const { return int(m_scale * 100); }
    bool hasPixmap() const { return m_origPix != nullptr; }

signals:
    void navFile(int delta);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupPlayer();
    void showImage(const QString &path);
    void showVideo(const QString &path);
    void showAudio(const QString &path);
    // #82:Markdown 以渲染后的 HTML 展示;PDF 走 Ghostscript 渲染 + 页导航
    void showMarkdown(const QString& path);
    void showPdf(const QString& path);
    void renderPdfPage();          // 后台渲染当前页(不卡 UI)
    void pdfGotoPage(int page);
    void updatePdfBar();
    void fitAuto();
    void render();
    // 唯一安全销毁出口：deleteLater + 置空，绝不在信号槽内同步 delete sender
    void teardownPlayer();
    // Live Photo 播完：安全回收播放器后切回静态图
    void finishLivePhoto();
    // 删除本会话从内嵌 Motion Photo 提取的临时视频
    void cleanupExtractCache();
    void toggleFullscreen();

    QString m_filePath;
    QString m_mode; // "image", "video", "audio", "none"
    QString m_livePhotoOriginalPath;
    bool m_isLivePhoto = false; // 当前播放的是动态照片视频（区别于普通视频）
    QPixmap *m_origPix = nullptr;
    double m_scale = 1.0;
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用,配合 resetAutoOnNav)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    bool m_dragging = false;
    QPointF m_dragStart, m_dragLabelPos;

    QLabel *m_imgLabel;
    QLabel *m_audioLabel;
    QLabel *m_placeholder = nullptr;   // 空态占位
    QWidget *m_videoWidget;
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QWidget *m_controlBar;
    QPushButton *m_btnPlay;
    QToolButton *m_btnVolume = nullptr;   // 音量按钮(点击弹竖向滑条)
    QToolButton *m_btnPrev = nullptr;     // 上一文件按钮(播放条最左,仿 XnView)
    QSlider *m_progress;
    QLabel *m_timeLabel;
    bool m_timeRemaining = false;          // 时间显示:已播/总时长(false) ↔ 剩余/总时长(true)

    QLabel *m_liveBadge = nullptr;   // 叠放于 videoWidget 右上角的 LIVE 徽章
    QMap<QString, QString> m_extractCache; // 图片路径 → 已提取的临时视频路径
    bool m_fullscreenMode = false;
    bool m_ctrlZoomed = false;   // Ctrl+滚轮缩放过(左键变为纯拖动)
    bool m_tempZoom   = false;   // 左键临时 1:1 放大(松开还原)
};
