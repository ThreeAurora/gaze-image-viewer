#include "thumbnailer.h"
#include "constants.h"
#include "namesort.h"
#include "settings.h"
#include "imgproc.h"
#include "wicdecode.h"
#include "logger.h"

#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <QPainter>
#include <QPainterPath>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QCryptographicHash>
#include <QPixmap>
#include <QImageReader>
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>

// ── 前置声明 ──
static QImage windowsShellThumb(const QString& filePath, int size);
#include <wincodec.h>
#include <QDateTime>
#include <QVariant>
#include <QImageReader>
#include <QBuffer>
#include <QFile>
#include <chrono>
#include <cstring>
#include <cmath>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

#include "thumbnailer_internal.h"

// ── 本编译单元(#129 从 thumbnailer.cpp 拆出):单例 / 线程池 / Prefs 快照 / enqueue-generate 调度主干 / ThumbTask ──

// ═══════════════════════════════════════════
// Singleton
// ═══════════════════════════════════════════
Thumbnailer& Thumbnailer::instance() {
    static Thumbnailer inst;
    return inst;
}

Thumbnailer::Thumbnailer() {
    m_pool = new QThreadPool(this);
    m_pool->setMaxThreadCount(4);
    // 本对象在 FileGrid 构造时(Main 线程)创建:快照只在此线程读 QSettings,
    // worker 线程读副本 —— 逐条目 enqueue 路径不碰设置/磁盘
    snapshotPrefs();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        snapshotPrefs();
    });
}

Thumbnailer::~Thumbnailer() {
    m_pool->waitForDone();
}

void Thumbnailer::snapshotPrefs() {
    AppSettings& st = AppSettings::instance();
    Prefs p;
    p.inDb        = st.get("Cache/thumbInDB", true).toBool();
    p.capOn       = st.get("Cache/maxCacheOn", true).toBool();
    p.maxDbMB     = qBound(64, st.get("Cache/maxCacheMB", 500).toInt(), 10240);
    p.dbCacheMB   = qBound(8, st.get("Cache/dbCacheMB", 64).toInt(), 8192);
    p.blobCodec   = qBound(0, st.get("Cache/compression", 4).toInt(), 4);
    p.highQuality = st.get("Thumbs/highQuality", true).toBool();
    p.framePct    = qBound(0, st.get("Thumbs/videoFramePct", 0).toInt(), 100);
    // ── 设置→缩略图(创建) ──
    p.useEmbedded   = st.get("Thumbs/useEmbedded", true).toBool();
    p.embedFallback = st.get("Thumbs/embedFallback", true).toBool();
    p.folder4       = st.get("Thumbs/folder4", true).toBool();
    p.video4        = st.get("Thumbs/video4", false).toBool();
    p.wholeFolder   = st.get("Thumbs/wholeFolder", false).toBool();
    // ── 设置→缩略图(处理) ──
    p.alpha     = st.get("Thumbs/alpha", true).toBool();
    p.transGrid = st.get("Thumbs/transparencyGrid", true).toBool();
    p.sharpen   = st.get("Thumbs/sharpen", false).toBool();
    p.gamma     = st.get("Thumbs/gamma", false).toBool();
    // ── 设置→缓存数据库 ──
    p.useCatalog   = st.get("Cache/useCatalog", true).toBool();
    p.checkStartup = st.get("Cache/checkOnStartup", false).toBool();
    QMutexLocker lk(&m_prefMutex);
    m_prefs = p;
}

Thumbnailer::Prefs Thumbnailer::prefs() const {
    QMutexLocker lk(&m_prefMutex);
    return m_prefs;
}

// ═══════════════════════════════════════════
// 公开接口
// ═══════════════════════════════════════════
void Thumbnailer::enqueue(const QString& filePath, int size, bool isVideo) {
    QString ck = cacheKey(filePath, size, isVideo);

    // 检查内存缓存
    QFileInfo fi(filePath);
    if (!fi.exists()) return;
    double mtime = fi.lastModified().toSecsSinceEpoch();

    {
        QImage cached;
        if (cacheLookup(ck, mtime, cached)) {
            emit thumbnailReady(filePath, cached);
            return;
        }
    }

    // 去重
    {
        QMutexLocker lk(&m_queueMutex);
        if (m_pending.count(ck)) return;
        m_pending.insert(ck);
    }

    auto* task = new ThumbTask(filePath, size, isVideo);
    task->setAutoDelete(true);
    m_pool->start(task);
}

void Thumbnailer::clearQueue() {
    m_pool->clear();
    QMutexLocker lk(&m_queueMutex);
    m_pending.clear();
}

// ═══════════════════════════════════════════
// 同步生成（后台线程调用）
// ═══════════════════════════════════════════
QImage Thumbnailer::generate(const QString& filePath, int size, bool isVideo) {
    QFileInfo fi(filePath);
    if (!fi.exists()) return {};

    // 请求尺寸=卡片缩略图盒子的实际宽度,缓存原样落库:存小了再放大必糊。
    // (旧版这里按 Cache/thumbWidth 465 上限砍请求,大卡片模式下 512→365
    //  再放大回 512,是缩略图发糊的直接根源,已移除)
    QString ck = cacheKey(filePath, size, isVideo);
    double mtime = fi.lastModified().toSecsSinceEpoch();
    Prefs p = prefs();

    // 查内存缓存
    {
        QImage cached;
        if (cacheLookup(ck, mtime, cached)) {
            QMutexLocker lk(&m_queueMutex);
            m_pending.erase(ck);
            return cached;
        }
    }

    // 查 SQLite 持久缓存(重启后免重新生成;格式自动识别,兼容旧 PNG/新 WebP 条目)
    // Cache/useCatalog=关 → 总开关关闭,完全不碰库;再按 Cache/thumbInDB 决定是否落库
    if (p.useCatalog && p.inDb) {
        QSqlDatabase db = th_impl::threadDb(p.dbCacheMB);
        if (db.isOpen()) {
            QSqlQuery q(db);
            q.prepare("SELECT png, mtime FROM thumbs WHERE key = ?");
            q.addBindValue(QVariant(ck));
            if (q.exec() && q.next()
                && std::abs(q.value(1).toDouble() - mtime) < 0.001) {
                QImage cached = QImage::fromData(q.value(0).toByteArray());
                if (!cached.isNull()) {
                    QMutexLocker lk(&m_queueMutex);
                    m_pending.erase(ck);
                    // 回填内存缓存(不重写库;超内存上限则跳过回填,下次淘汰自然腾位)
                    QMutexLocker lk2(&m_cacheMutex);
                    int64_t pixBytes = static_cast<int64_t>(cached.width())
                                     * cached.height() * 4;
                    if (m_memCacheBytes + pixBytes <= MAX_MEM_CACHE) {
                        CacheEntry entry;
                        entry.pixmap = cached;
                        entry.mtime = mtime;
                        entry.lastAccess =
                            std::chrono::steady_clock::now().time_since_epoch().count();
                        m_memCache[ck] = entry;
                        m_memCacheBytes += pixBytes;
                    }
                    return cached;
                }
            }
        }
    }

    // 生成(分三条线:文件夹拼图 / 视频 / 图片)
    QImage pix;
    if (fi.isDir()) {
        // Thumbs/folder4:目录缩略图(内部再按开关决定 2x2 还是单封面)
        pix = folderThumb(filePath, size);
    } else if (isVideo) {
        // Thumbs/video4:四帧拼图;关则单帧(原行为)。
        // 拼图内部已过 postProcess,这里用 done 标记避免二次处理;
        // 此前 Shell 回退路径漏了后处理(alpha/锐化没套上),已修
        bool done = false;
        if (p.video4) {
            pix = videoContactSheet(filePath, size);
            done = !pix.isNull();
        }
        if (!done) {
            pix = videoThumbFFmpeg(filePath, size);
            if (pix.isNull()) pix = th_impl::windowsShellThumb(filePath, size);
            if (!pix.isNull()) pix = postProcess(pix, size);
        }
    } else {
        const bool useReady = p.useEmbedded;   // Thumbs/useEmbedded
        // Shell 缓存清晰档位只到 256;更大的请求它会把 256 缓存放大到精确尺寸
        // 返回,embedFallback 的"小于目标"检查拦不住 → 大卡片发糊。
        // >256 一律从原图自解码,质量有保证且落库后同样秒开。
        if (useReady && size <= 256) {
            pix = th_impl::windowsShellThumb(filePath, size);
            // #242:exe/ico 一类的"缩略图"=图标本身,小档位图标会被 shell 贴在
            // 画布左上角,入库后显示成"左上角一小块" —— 裁透明边后原大小居中
            const QString suf = fi.suffix().toLower();
            if (suf == "exe" || suf == "dll" || suf == "ico" || suf == "scr"
                || suf == "msi" || suf == "cpl" || suf == "lnk" || suf == "ocx")
                pix = th_impl::trimPadCenter(pix);
        }
        // Thumbs/embedFallback:现成缩略图比目标尺寸小 → 视为不合格,从原图重做
        if (pix.isNull()
            || (p.embedFallback && (pix.width() < size && pix.height() < size))) {
            QImage fromSource = imageThumb(filePath, size);
            if (!fromSource.isNull()) pix = fromSource;
        }
        pix = postProcess(pix, size);
    }

    // 写入缓存(useCatalog 关时不落库)
    if (!pix.isNull() && p.useCatalog) {
        cacheStore(ck, pix, mtime);
    }

    QMutexLocker lk(&m_queueMutex);
    m_pending.erase(ck);
    return pix;
}

// ═══════════════════════════════════════════
// ThumbTask — QRunnable 实现
// ═══════════════════════════════════════════
void ThumbTask::run() {
    QImage pix = Thumbnailer::instance().generate(m_path, m_size, m_isVideo);
    if (!pix.isNull()) {
        QMetaObject::invokeMethod(
            &Thumbnailer::instance(),
            [path = m_path, p = std::move(pix)]() mutable {
                emit Thumbnailer::instance().thumbnailReady(path, p);
            },
            Qt::QueuedConnection);
    }
}
