#pragma once
#include <QObject>
#include <QImage>
#include <QThreadPool>
#include <QMutex>
#include <QSqlDatabase>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <cstdint>

class Thumbnailer : public QObject {
    Q_OBJECT
public:
    static Thumbnailer& instance();
    ~Thumbnailer();

    void enqueue(const QString& filePath, int size, bool isVideo);
    void clearQueue();
    QImage generate(const QString& filePath, int size, bool isVideo);
    // 缓存完整性校验(Cache/checkOnStartup):丢掉读不出来的坏条目
    void verifyCache();

    // Windows Shell 缩略图(供 PDF 等外部格式的预览回退使用)
    static QImage shellThumbFor(const QString& filePath, int size);

signals:
    // 注意：worker 线程生成，跨线程以 QImage 传递（QPixmap 仅限 GUI 线程），
    // 接收方（主线程）自行 QPixmap::fromImage
    void thumbnailReady(const QString& filePath, const QImage& image);

private:
    Thumbnailer();

    // ── 设置快照(设置→缓存 / 设置→缩略图) ──
    // QSettings 只在主线程读:enqueue() 里刷新快照,worker 线程读副本
    struct Prefs {
        bool inDb        = true;   // Cache/thumbInDB:关闭=只用内存缓存
        bool capOn       = true;   // Cache/maxCacheOn:关闭=不做容量上限淘汰
        int  maxDbMB     = 500;    // Cache/maxCacheMB
        int  dbCacheMB   = 64;     // Cache/dbCacheMB → SQLite page cache
        int  blobCodec   = 4;      // Cache/compression:0/1=png 2/3=jpg 4=webp
        bool highQuality = true;   // Thumbs/highQuality:关闭=快速缩放
        int  framePct    = 0;      // Thumbs/videoFramePct:0=固定取第 1 秒
        // ── 设置→缩略图(创建/处理) ──
        bool useEmbedded   = true;  // Thumbs/useEmbedded:优先取现成缩略图
        bool embedFallback = true;  // Thumbs/embedFallback:现成图偏小则从原图重做
        bool alpha         = true;  // Thumbs/alpha:保留 alpha 通道
        bool transGrid     = true;  // Thumbs/transparencyGrid:透明处画网格
        bool sharpen       = true;  // Thumbs/sharpen:轻度锐化
        bool gamma         = false; // Thumbs/gamma:线性光降采样(防明暗失真)
        bool folder4       = true;  // Thumbs/folder4:文件夹 2x2 拼图
        bool video4        = false; // Thumbs/video4:视频四帧拼图
        bool wholeFolder   = false; // Thumbs/wholeFolder:整目录预生成
        // ── 设置→缓存数据库 ──
        bool useCatalog    = true;  // Cache/useCatalog:总开关(关=完全不落库)
        bool checkStartup  = false; // Cache/checkOnStartup:启动后校验缓存完整性
    };
    void  snapshotPrefs();
    Prefs prefs() const;
    mutable QMutex     m_prefMutex;
    Prefs              m_prefs;

    // ── 设置快照(设置→缓存 / 设置→缩略图) ──
    // QSettings 只在主线程读:enqueue() 里刷新快照,worker 线程读副本
    struct Prefs {
        bool inDb        = true;   // Cache/thumbInDB:关闭=只用内存缓存
        bool capOn       = true;   // Cache/maxCacheOn:关闭=不做容量上限淘汰
        int  maxDbMB     = 500;    // Cache/maxCacheMB
        int  dbCacheMB   = 64;     // Cache/dbCacheMB → SQLite page cache
        int  blobCodec   = 4;      // Cache/compression:0/1=png 2/3=jpg 4=webp
        bool highQuality = true;   // Thumbs/highQuality:关闭=快速缩放
        int  framePct    = 0;      // Thumbs/videoFramePct:0=固定取第 1 秒
    };
    void  snapshotPrefs();
    Prefs prefs() const;
    mutable QMutex     m_prefMutex;
    Prefs              m_prefs;

    // ── 图片缩略图（QImage直接缩放） ──
    QImage imageThumb(const QString& filePath, int size);

    // ── 视频缩略图（FFmpeg C API）──
    // pctOverride >= 0 时忽略 Thumbs/videoFramePct,取指定百分比处(4 帧拼图用)
    QImage videoThumbFFmpeg(const QString& filePath, int size, int pctOverride = -1);
    // 回退方案：QProcess fork ffmpeg。pct 语义同上,由 videoThumbFFmpeg 统一算好后传下来
    QImage videoThumbFallback(const QString& filePath, int size, int pct);
    // 文件夹缩略图:外框画成文件夹,里面嵌内容图(Thumbs/folder4 开=2x2 四格,关=单张封面)
    // 目录内不足 4 张图就有几格画几格;一张都没有则返回空 → 卡片回落 folderIcon
    QImage folderThumb(const QString& dirPath, int size);
    // 视频四帧拼图(Thumbs/video4):按 framePct 起均匀取 4 帧
    QImage videoContactSheet(const QString& filePath, int size);

    // 后处理(设置→缩略图→处理):alpha/透明网格/锐化/gamma 统一出口
    QImage postProcess(QImage img, int size) const;

    // ── 缓存 ──
    QString cacheKey(const QString& filePath, int size) const;
    bool    cacheLookup(const QString& key, double mtime, QImage& out);
    void    cacheStore(const QString& key, const QImage& pix, double mtime);
    void    initDatabase();
    void    evictIfNeeded();

    // ── Pixmap → PNG bytes ──
    QByteArray pixmapToBlob(const QImage& pix) const;   // WebP q75(库内持久缩略图)

    QThreadPool* m_pool = nullptr;

    // 任务去重：已经在队列中的 (filePath, size) 不再重复添加
    QMutex                 m_queueMutex;
    std::unordered_set<QString> m_pending;

    // 内存 LRU 缓存
    QMutex m_cacheMutex;
    struct CacheEntry {
        QImage  pixmap;   // 线程安全的 QImage；转 QPixmap 在消费线程进行
        double  mtime = 0.0;
        int64_t lastAccess = 0;
    };
    std::unordered_map<QString, CacheEntry> m_memCache;
    int64_t m_memCacheBytes = 0;
    static constexpr int64_t MAX_MEM_CACHE = 200LL * 1024 * 1024; // 200MB

    // SQLite：连接按"每线程一个"管理（见 thumbnailer.cpp 的 threadDb()），
    // QSqlDatabase 连接禁止跨线程使用——这是 qsqlite.dll AV 崩溃的根因
};

// ── 后台缩略图任务 ──
class ThumbTask : public QRunnable {
public:
    ThumbTask(const QString& path, int size, bool isVideo)
        : m_path(path), m_size(size), m_isVideo(isVideo) {}
    void run() override;
private:
    QString m_path;
    int     m_size;
    bool    m_isVideo;
};
