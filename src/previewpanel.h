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
#include <QImage>
#include <QHash>
#include <QKeySequence>
#include <memory>

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
    // 相邻预读:切换方向键时预解码下一张/上一张,命中则零等待显示(mainwindow 调用)
    void preload(const QString& prev, const QString& next);
    // 面板身份:浏览器预览窗格 ↔ 独立查看器(背景色各用一个设置项)
    Q_INVOKABLE void setViewerMode(bool on);

signals:
    void navFile(int delta);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool eventFilter(QObject *obj, QEvent *event) override;

private:
    void setupPlayer();
    void ensureVideoWidget();   // 视频控件唯一创建/复用入口
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
    // 设置活应用:背景色/挡板底纹/图片边框(设置→查看→背景与界面元素)
    void applyBackdrop();
    // Viewer/autoFit → 目标缩放系数(见 previewpanel.cpp 的取值语义表)
    double fitScaleFor(const QSize& viewSize) const;
    // 唯一安全销毁出口：deleteLater + 置空，绝不在信号槽内同步 delete sender
    void teardownPlayer();
    // GIF QMovie 安全回收(旧实现每次 new 从不 delete,看一次泄一个)
    void stopMovie();
    // GIF QMovie 安全回收(旧实现每次 new 从不 delete,看一次泄一个)
    void stopMovie();
    // Live Photo 播完：安全回收播放器后切回静态图
    void finishLivePhoto();
    // 删除本会话从内嵌 Motion Photo 提取的临时视频
    void cleanupExtractCache();
    // 图片后台解码:切换操作立即返回(UI 不卡),解码完成后一次性全清晰显示
    // (用户定版:异步但无低清过渡态——否决"先糊后清",也否决同步卡顿)
    void decodeFullAsync(const QString& path, quint64 gen);
    void onFullDecoded(std::shared_ptr<QImage> img, const QString& path, quint64 gen);
    void applyImage(const QImage& img);        // 应用全图到视图(fit+显示)
    void preloadNext();
    // Live Photo 内嵌视频后台提取(ffmpeg remux 秒级,不再阻塞:
    // 先显示静态图,提取完成自动切播放)
    void startExtractAsync(const QString& path, const LivePhoto::Info& info);

    QString m_filePath;
    QString m_mode; // "image", "video", "audio", "none"
    QString m_livePhotoOriginalPath;
    bool m_isLivePhoto = false; // 当前播放的是动态照片视频（区别于普通视频）
    QPixmap *m_origPix = nullptr;
    double m_scale = 1.0;
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用,配合 resetAutoOnNav)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    bool m_dragging = false;
    QPointF m_dragStart, m_dragLabelPos;

    QLabel *m_imgLabel;
    QLabel *m_audioLabel;
    QLabel *m_placeholder = nullptr;   // 空态占位
    QMovie *m_movie = nullptr;         // GIF 动画(切换时 stop+deleteLater,防泄漏)
    QWidget *m_videoWidget;
    QVideoWidget *m_vw = nullptr;   // 复用的视频控件(切视频不重建,杜绝叠加透出窗口期)
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QWidget *m_controlBar;
    QPushButton *m_btnPlay;
    QToolButton *m_btnVolume = nullptr;   // 音量按钮(点击弹竖向滑条)
    QPushButton *m_btnPrev = nullptr;     // 上一文件按钮(播放条最左,仿 XnView)
    QSlider *m_progress;
    QLabel *m_timeLabel;
    bool m_timeRemaining = false;          // 时间显示:已播/总时长(false) ↔ 剩余/总时长(true)

    QLabel *m_liveBadge = nullptr;   // 叠放于 videoWidget 右上角的 LIVE 徽章
    QMap<QString, QString> m_extractCache; // 图片路径 → 已提取的临时视频路径
    bool m_ctrlZoomed = false;   // Ctrl+滚轮缩放过(左键变为纯拖动)
    bool m_tempZoom   = false;   // 左键临时 1:1 放大(松开还原)

    // 图片后台解码状态
    quint64 m_imgReqGen = 0;    // 最新请求代号(loadFile/showImage 递增,切走即作废在途结果)
    quint64 m_issuedGen = 0;    // 当前后台任务对应的代号
    bool    m_fullBusy  = false; // 解码进行中(同一时刻最多一个,控内存峰值)

    // 查看器快捷键表(ini ViewerShortcut/*;设置页"交互→快捷键→查看器"可改)
    QHash<QString, QKeySequence> m_viewerHotkeys;   // 动作名 → 键序
    bool  m_hotkeysLoaded = false;
    void  ensureHotkeys();
    QString hotkeyAction(QKeyEvent* e) const;       // 键事件 → 动作名(未命中返回空)

    // 相邻预读状态(上限 2 张:上一张+下一张;方向键切换零等待)
    QStringList m_preloadQueue;            // 待预载路径(下一张优先)
    QMap<QString, QImage> m_preloadCache;  // 预载结果
    QStringList m_preloadOrder;            // 插入序(逐出最旧)
    bool m_preloadBusy = false;
public:
    void reloadViewerHotkeys();                     // 设置页修改后刷新缓存
};
