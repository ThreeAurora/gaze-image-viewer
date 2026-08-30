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
#include <QVector>
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
    // 预览当前显示的文件(空=没有)。调用方用它挡掉"对同一张再解一遍"
    const QString& filePath() const { return m_filePath; }
    // 预览当前显示的文件(空=没有)。调用方用它挡掉"对同一张再解一遍"
    const QString& filePath() const { return m_filePath; }
    void clear();
    void togglePlayPause();
    void seekDelta(int seconds);
    // 相邻预读:切换方向键时预解码下一张/上一张,命中则零等待显示(mainwindow 调用)
    void preload(const QString& prev, const QString& next);
    // 面板身份:浏览器预览窗格 ↔ 独立查看器(背景色各用一个设置项)
    Q_INVOKABLE void setViewerMode(bool on);
    // 该键事件是否命中 ViewerShortcut/* 表。主窗口的应用级过滤器用它让路:
    // 查看器里默认表和浏览器键位撞车("适应窗口"=F，浏览器 F=红标)
    bool claimsHotkey(QKeyEvent* e);
    // 该键事件是否命中 ViewerShortcut/* 表。主窗口的应用级过滤器用它让路:
    // 查看器里默认表和浏览器键位撞车("适应窗口"=F，浏览器 F=红标)
    bool claimsHotkey(QKeyEvent* e);

signals:
    void navFile(int delta);

protected:
    void resizeEvent(QResizeEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
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
    void raiseVideoCover();     // 升起纯黑遮罩(盖住控件残帧,Playing 后收回)
    void syncVideoChildren();   // vw/cover 几何同步到 videoWidget(布局激活后必须重跑)
    void syncVideoChildren();   // vw/cover 几何同步到 videoWidget(布局激活后必须重跑)
    void syncVideoChildren();   // vw/cover 几何同步到 videoWidget(布局激活后必须重跑)
    void syncVideoChildren();   // vw/cover 几何同步到 videoWidget(布局激活后必须重跑)
    void raiseVideoCover();     // 升起纯黑遮罩(盖住控件残帧,Playing 后收回)
    void showImage(const QString &path);
    void showGif(const QString &path);        // GIF:第一帧定几何,动画只换像素(#103/#96)
    void blitMovieFrame();                    // GIF 取帧唯一出口:当前帧缩到 label 尺寸
    // ── GIF 走视频那套形态(#97):同一条控制栏 + 时间轴 + 播放暂停 ──
    void buildGifTimeline();                  // 逐帧时长表 → 进度条范围 + 时长文本
    void gifSyncToFrame(int f);               // 帧号 → 播放头(进度条 + 时间文本)
    void gifSeekMs(int ms);                   // 播放头 → 帧号 → jumpToFrame + 出图
    void progressScrub(qreal x);              // 进度条擦洗公共落点(按下/拖动共用)
    void progressScrub(qreal x);              // 进度条擦洗公共落点(按下/拖动共用)
    void progressScrub(qreal x);              // 进度条擦洗公共落点(按下/拖动共用)
    void progressScrub(qreal x);              // 进度条擦洗公共落点(按下/拖动共用)
    void setGifPaused(bool p);                // GIF 播放/暂停唯一出口(按钮/单击/空格)
    void applyGifChrome();                    // 控制栏显隐 + 音量键(GIF 没有音轨)
    // ── GIF 走视频那套形态(#97):同一条控制栏 + 时间轴 + 播放暂停 ──
    void buildGifTimeline();                  // 逐帧时长表 → 进度条范围 + 时长文本
    void gifSyncToFrame(int f);               // 帧号 → 播放头(进度条 + 时间文本)
    void gifSeekMs(int ms);                   // 播放头 → 帧号 → jumpToFrame + 出图
    void setGifPaused(bool p);                // GIF 播放/暂停唯一出口(按钮/单击/空格)
    void applyGifChrome();                    // 控制栏显隐 + 音量键(GIF 没有音轨)
    // ── GIF 走视频那套形态(#97):同一条控制栏 + 时间轴 + 播放暂停 ──
    void buildGifTimeline();                  // 逐帧时长表 → 进度条范围 + 时长文本
    void gifSyncToFrame(int f);               // 帧号 → 播放头(进度条 + 时间文本)
    void gifSeekMs(int ms);                   // 播放头 → 帧号 → jumpToFrame + 出图
    void setGifPaused(bool p);                // GIF 播放/暂停唯一出口(按钮/单击/空格)
    void applyGifChrome();                    // 控制栏显隐 + 音量键(GIF 没有音轨)
    // ── GIF 走视频那套形态(#97):同一条控制栏 + 时间轴 + 播放暂停 ──
    void buildGifTimeline();                  // 逐帧时长表 → 进度条范围 + 时长文本
    void gifSyncToFrame(int f);               // 帧号 → 播放头(进度条 + 时间文本)
    void gifSeekMs(int ms);                   // 播放头 → 帧号 → jumpToFrame + 出图
    void setGifPaused(bool p);                // GIF 播放/暂停唯一出口(按钮/单击/空格)
    void applyGifChrome();                    // 控制栏显隐 + 音量键(GIF 没有音轨)
    void showImageHint(const QString &text);  // 无可显示位图:label 收成一格提示条,不沿用上图尺寸
    void showGif(const QString &path);        // GIF:第一帧定几何,动画只换像素(#103/#96)
    void blitMovieFrame();                    // GIF 取帧唯一出口:当前帧缩到 label 尺寸
    void showImageHint(const QString &text);  // 无可显示位图:label 收成一格提示条,不沿用上图尺寸
    void showGif(const QString &path);        // GIF:第一帧定几何,动画只换像素(#103/#96)
    void blitMovieFrame();                    // GIF 取帧唯一出口:当前帧缩到 label 尺寸
    void showImageHint(const QString &text);  // 无可显示位图:label 收成一格提示条,不沿用上图尺寸
    void showGif(const QString &path);        // GIF:第一帧定几何,动画只换像素(#103/#96)
    void blitMovieFrame();                    // GIF 取帧唯一出口:当前帧缩到 label 尺寸
    void showImageHint(const QString &text);  // 无可显示位图:label 收成一格提示条,不沿用上图尺寸
    void showVideo(const QString &path);
    void showAudio(const QString &path);
    void showText(const QString &path);
    void showText(const QString &path);
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
    void restoreCursor();             // Fullscreen/hideCursor:指针移动即恢复
    QColor backdropColor() const;   // 查看器/浏览器预览各用一个背景色设置
    bool   inFullscreen() const;    // 所在窗口处于全屏 = 套用 Fullscreen/* 设置
    QString modeKey(const char* suffix) const;  // "Viewer/xxx" ↔ "Fullscreen/xxx"
    // Viewer/autoFit → 目标缩放系数(见 previewpanel.cpp 的取值语义表)
    double fitScaleFor(const QSize& viewSize) const;
    // 拖拽平移约束:图片在某轴不超出预览框 → 该轴锁死居中(两侧黑边等宽),
    // 只允许在溢出的轴平移,且平移到图片边缘即停(不露白边)。
    QPoint clampedLabelPos(QPoint p) const;
    // 拖拽平移约束:图片在某轴不超出预览框 → 该轴锁死居中(两侧黑边等宽),
    // 只允许在溢出的轴平移,且平移到图片边缘即停(不露白边)。
    QPoint clampedLabelPos(QPoint p) const;
    // 设置活应用:背景色/挡板底纹/图片边框(设置→查看→背景与界面元素)
    void applyBackdrop();
    void restoreCursor();             // Fullscreen/hideCursor:指针移动即恢复
    QColor backdropColor() const;   // 查看器/浏览器预览各用一个背景色设置
    bool   inFullscreen() const;    // 所在窗口处于全屏 = 套用 Fullscreen/* 设置
    QString modeKey(const char* suffix) const;  // "Viewer/xxx" ↔ "Fullscreen/xxx"
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
    // 动态照片:单击静态预览=重播动态部分(不做临时 1:1 放大)
    void playLivePhoto();
    // 无预览出口:目录 + 未知类型共用(清空各视图,只留占位底)
    void showNoPreview();
    // 导航小窗拖动:指尖下的缩略图点 → 视口中心(几何与 updatePanTool 同一套)
    void panNavTo(const QPoint& thumbPos);
    QRect navPixmapRect() const;           // 缩略图 pixmap 的实际摆放矩形(m_panTool 坐标)
    QRect navPixmapRect() const;           // 缩略图 pixmap 的实际摆放矩形(m_panTool 坐标)
    QRect navPixmapRect() const;           // 缩略图 pixmap 的实际摆放矩形(m_panTool 坐标)
    QRect navPixmapRect() const;           // 缩略图 pixmap 的实际摆放矩形(m_panTool 坐标)
    // 动态照片:单击静态预览=重播动态部分(不做临时 1:1 放大)
    void playLivePhoto();
    // 无预览出口:目录 + 未知类型共用(清空各视图,只留占位底)
    void showNoPreview();
    // 导航小窗拖动:指尖下的缩略图点 → 视口中心(几何与 updatePanTool 同一套)
    void panNavTo(const QPoint& thumbPos);

    // ── 设置活接线:查看器/全屏的界面元素 ──
    void applyViewerChrome();              // 改设置/换文件后统一刷新下列元素
    void updateOverlayScrollbars();        // Viewer|Fullscreen/showScrollbar
    void updateInfoBar();                  // Fullscreen/showInfo
    void updateFloatBar(const QPoint* cursor = nullptr); // Fullscreen/showToolbar + floatView
    void updatePanTool();                  // Viewer/panTool 平移导航小窗
    void updateSelectionHighlight();       // Viewer/showBorder 白框;蓝框已删(2026-08-30)
    void updateRatingBadge();              // Viewer/showRating 颜色标记点
    double oneToOneScale() const;          // 1:1 = 1 图像像素 : 1 屏幕像素
    double stepZoom(double cur, bool up) const;  // Viewer/zoomMode=0 的固定档位
    // 以屏幕锚点为中心缩放:锚点下的图像点缩放前后停在原地(滚轮/长按都走这里)
    void zoomAnchored(double newScale, const QPoint& anchor);
    double pixelAspect() const;            // Viewer/pixelRatio 像素比
    void playAudioCompanion(const QString& imagePath);  // Viewer/autoPlayAudioCompanion
    // Viewer/gamma + Viewer/sharpen 的显示后处理(结果按目标尺寸缓存)
    const QPixmap& processedFor(int w, int h, const QPixmap& src);

    QString m_filePath;
    QString m_mode; // "image", "video", "audio", "none"
    QString m_livePhotoOriginalPath;
    bool m_isLivePhoto = false; // 当前播放的是动态照片视频（区别于普通视频）
    std::optional<LivePhoto::Info> m_liveInfo;  // 当前文件若为动态照片,单击=重播
    bool m_extractBusy = false; // 内嵌视频提取中(同一时刻最多一次,连点不叠任务)
    bool m_navDragging = false; // 导航小窗蓝框拖动中
    std::optional<LivePhoto::Info> m_liveInfo;  // 当前文件若为动态照片,单击=重播
    bool m_extractBusy = false; // 内嵌视频提取中(同一时刻最多一次,连点不叠任务)
    bool m_navDragging = false; // 导航小窗蓝框拖动中
    QPixmap *m_origPix = nullptr;
    double m_scale = 1.0;
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    double m_lastScale = 0.0;   // 上次实际应用的缩放(Viewer/autoFit=0"上次使用过的"用)
    bool   m_viewerMode = false; // 独立查看器(true)/浏览器预览窗格(false)
    bool m_dragging = false;
    QPointF m_dragStart, m_dragLabelPos;

    QLabel *m_imgLabel;
    QLabel *m_audioLabel;
    QLabel *m_placeholder = nullptr;   // 空态占位
    QTextEdit *m_textEdit = nullptr;   // txt 文本预览
    QTextEdit *m_textEdit = nullptr;   // txt 文本预览
    QMovie *m_movie = nullptr;         // GIF 动画(切换时 stop+deleteLater,防泄漏)
    // GIF 时间轴状态(#97)。逐帧时长自己解析文件字节:QMovie/QImageReader 在
    // Qt 6.5.3 都不暴露 per-frame delay,而时长表与帧数必须同一次扫描得出,
    // 否则进度条刻度会和实际播放的帧对不上
    bool m_isGif = false;
    bool m_gifPaused = false;
    QVector<int> m_gifDelay;           // 每帧时长(ms)
    QVector<int> m_gifStart;           // 每帧起始时间(ms),长度 = m_gifDelay + 1
    QPoint m_gifPressPos;              // 单击=播放/暂停:按下点与松开点足够近才算单击
    bool   m_gifToggleArm = false;

    // GIF 时间轴状态(#97)。逐帧时长自己解析文件字节:QMovie/QImageReader 在
    // Qt 6.5.3 都不暴露 per-frame delay,而时长表与帧数必须同一次扫描得出,
    // 否则进度条刻度会和实际播放的帧对不上
    bool m_isGif = false;
    bool m_gifPaused = false;
    QVector<int> m_gifDelay;           // 每帧时长(ms)
    QVector<int> m_gifStart;           // 每帧起始时间(ms),长度 = m_gifDelay + 1
    QPoint m_gifPressPos;              // 单击=播放/暂停:按下点与松开点足够近才算单击
    bool   m_gifToggleArm = false;

    // GIF 时间轴状态(#97)。逐帧时长自己解析文件字节:QMovie/QImageReader 在
    // Qt 6.5.3 都不暴露 per-frame delay,而时长表与帧数必须同一次扫描得出,
    // 否则进度条刻度会和实际播放的帧对不上
    bool m_isGif = false;
    bool m_gifPaused = false;
    QVector<int> m_gifDelay;           // 每帧时长(ms)
    QVector<int> m_gifStart;           // 每帧起始时间(ms),长度 = m_gifDelay + 1
    QPoint m_gifPressPos;              // 单击=播放/暂停:按下点与松开点足够近才算单击
    bool   m_gifToggleArm = false;

    // GIF 时间轴状态(#97)。逐帧时长自己解析文件字节:QMovie/QImageReader 在
    // Qt 6.5.3 都不暴露 per-frame delay,而时长表与帧数必须同一次扫描得出,
    // 否则进度条刻度会和实际播放的帧对不上
    bool m_isGif = false;
    bool m_gifPaused = false;
    QVector<int> m_gifDelay;           // 每帧时长(ms)
    QVector<int> m_gifStart;           // 每帧起始时间(ms),长度 = m_gifDelay + 1
    QPoint m_gifPressPos;              // 单击=播放/暂停:按下点与松开点足够近才算单击
    bool   m_gifToggleArm = false;

    QWidget *m_videoWidget;
    QVideoWidget *m_vw = nullptr;   // 复用的视频控件(切视频不重建,杜绝叠加透出窗口期)
    QWidget *m_videoCover = nullptr;   // 纯黑遮罩:attach→新视频首帧之间盖住控件里的残帧
    QWidget *m_videoCover = nullptr;   // 纯黑遮罩:attach→新视频首帧之间盖住控件里的残帧
    QMediaPlayer *m_player = nullptr;
    QAudioOutput *m_audioOutput = nullptr;
    // 延迟 attach:setSource 后不立刻接输出/播,等 mediaStatus 就绪再接
    QString m_pendingPlay;     // 已 setSource、待就绪 attach+play 的源
    bool m_videoOutAttached = false;   // 视频输出当前是否已接到 m_vw
                                       // (same-src 重播若输出已断必须接回,否则只出声不出画)
    // 延迟 attach:setSource 后不立刻接输出/播,等 mediaStatus 就绪再接
    QString m_pendingPlay;     // 已 setSource、待就绪 attach+play 的源
    bool m_videoOutAttached = false;   // 视频输出当前是否已接到 m_vw
                                       // (same-src 重播若输出已断必须接回,否则只出声不出画)
    QElapsedTimer m_navClock;  // 最近一次导航时刻:浏览扫动时暂缓自动播(停稳才切视频)
    QWidget *m_controlBar;
    QWidget *m_imgSpace;   // 图片/GIF 形态的空间吸收器:没有它 40px 的控制栏会被布局垂直居中
    QWidget *m_imgSpace;   // 图片/GIF 形态的空间吸收器:没有它 40px 的控制栏会被布局垂直居中
    QWidget *m_imgSpace;   // 图片/GIF 形态的空间吸收器:没有它 40px 的控制栏会被布局垂直居中
    QWidget *m_imgSpace;   // 图片/GIF 形态的空间吸收器:没有它 40px 的控制栏会被布局垂直居中
    QPushButton *m_btnPlay;
    QPushButton *m_btnStop = nullptr;     // 停止(位置归零,再播从头开始)
    QPushButton *m_btnStop = nullptr;     // 停止(位置归零,再播从头开始)
    QPushButton *m_btnStop = nullptr;     // 停止(位置归零,再播从头开始)
    QPushButton *m_btnStop = nullptr;     // 停止(位置归零,再播从头开始)
    QToolButton *m_btnVolume = nullptr;   // 音量按钮(点击弹竖向滑条)
    QPushButton *m_btnPrev = nullptr;     // 上一文件按钮(播放条最左,仿 XnView)
    QSlider *m_progress;
    QLabel *m_timeLabel;
    bool m_timeRemaining = false;          // 时间显示:已播/总时长(false) ↔ 剩余/总时长(true)

    QLabel *m_liveBadge = nullptr;   // 叠放于 videoWidget 右上角的 LIVE 徽章
    QMap<QString, QString> m_extractCache; // 图片路径 → 已提取的临时视频路径
    bool m_ctrlZoomed = false;   // Ctrl+滚轮缩放过(左键变为纯拖动)
    bool m_tempZoom   = false;   // 左键临时 1:1 放大(松开还原)
    bool m_rbtnWheel  = false;   // 右键+滚轮缩放过:松开右键那次不弹上下文菜单
    bool m_rbtnWheel  = false;   // 右键+滚轮缩放过:松开右键那次不弹上下文菜单
    bool m_rbtnWheel  = false;   // 右键+滚轮缩放过:松开右键那次不弹上下文菜单
    bool m_rbtnWheel  = false;   // 右键+滚轮缩放过:松开右键那次不弹上下文菜单
    QTimer m_cursorTimer;        // Fullscreen/hideCursor:指针静止一段时间后隐藏
    bool   m_cursorHidden = false;
    QTimer m_cursorTimer;        // Fullscreen/hideCursor:指针静止一段时间后隐藏
    bool   m_cursorHidden = false;

    // 图片后台解码状态
    quint64 m_imgReqGen = 0;    // 最新请求代号(loadFile/showImage 递增,切走即作废在途结果)
    quint64 m_issuedGen = 0;    // 当前后台任务对应的代号
    bool    m_fullBusy  = false; // 解码进行中(同一时刻最多一个,控内存峰值)

    // 查看器快捷键表(ini ViewerShortcut/*;设置页"交互→快捷键→查看器"可改)
    QHash<QString, QKeySequence> m_viewerHotkeys;   // 动作名 → 键序
    bool  m_hotkeysLoaded = false;
    void  ensureHotkeys();
    QString hotkeyAction(QKeyEvent* e) const;       // 键事件 → 动作名(未命中返回空)

    // ── 查看器/全屏界面元素(全部由对应设置项控制显隐) ──
    QScrollBar* m_hScroll   = nullptr;     // Viewer|Fullscreen/showScrollbar
    QScrollBar* m_vScroll   = nullptr;
    QLabel*     m_infoLabel = nullptr;     // Fullscreen/showInfo
    QString     m_infoFileKey;             // 信息条文件部分缓存键
    QString     m_infoBase;                // 信息条"文件名 WxH 体积"缓存
    QWidget*    m_floatBar  = nullptr;     // Fullscreen/showToolbar + floatView
    QWidget*    m_panTool   = nullptr;     // Viewer/panTool
    QLabel*     m_panThumb  = nullptr;
    QLabel*     m_ratingDot = nullptr;     // Viewer/showRating 颜色标记点
    QLabel*     m_ratingDot = nullptr;     // Viewer/showRating 颜色标记点
    QWidget*    m_panView   = nullptr;     // 导航小窗里的视口指示框
    bool  m_navigating   = false;          // Viewer/resetAutoOnNav:本次是切文件
    int   m_labelStyleState = -1;          // 图片标签样式缓存(避免 resize 时反复 setStyleSheet)
    bool  m_secondPass   = false;          // Viewer/twoPassRender 第二遍标记
    QString  m_panKey;                     // 导航小窗缩略图对应的文件
    QPixmap m_procPix;                     // gamma/sharpen 后处理结果
    QString  m_procKey;                    // 缓存键 "<w>x<h>"
    double m_procScale   = 0.0;            // 生成 m_procPix 时的缩放比

    // 相邻预读状态(上限 2 张:上一张+下一张;方向键切换零等待)
    QStringList m_preloadQueue;            // 待预载路径(下一张优先)
    QMap<QString, QImage> m_preloadCache;  // 预载结果
    QStringList m_preloadOrder;            // 插入序(逐出最旧)
    bool m_preloadBusy = false;
public:
    // 设置页修改后刷新缓存。设置页不 include 本头文件,靠元调用通知 → 必须 Q_INVOKABLE
    Q_INVOKABLE void reloadViewerHotkeys();
};
