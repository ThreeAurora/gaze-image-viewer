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
    };
    void  snapshotPrefs();
    Prefs prefs() const;
    mutable QMutex     m_prefMutex;
    Prefs              m_prefs;

    // ── 图片缩略图（QImage直接缩放） ──
    QImage imageThumb(const QString& filePath, int size);

    // ── 视频缩略图（FFmpeg C API）──
    QImage videoThumbFFmpeg(const QString& filePath, int size);
    // 回退方案：QProcess fork ffmpeg
    QImage videoThumbFallback(const QString& filePath, int size);

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
    static constexpr int64_t MAX_DB_MB = 500;
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
