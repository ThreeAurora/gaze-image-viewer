#include "thumbnailer.h"
#include "constants.h"
#include "settings.h"
#include "imgproc.h"
#include "wicdecode.h"

#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <QPainter>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryFile>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QPixmap>
#include <QImageReader>

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
}

Thumbnailer::~Thumbnailer() {
    m_pool->waitForDone();
}

// ═══════════════════════════════════════════
// 公开接口
// ═══════════════════════════════════════════
void Thumbnailer::enqueue(const QString& filePath, int size, bool isVideo) {
    QString ck = cacheKey(filePath, size);

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

    QString ck = cacheKey(filePath, size);
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
    // Cache/thumbInDB=关 → 只用内存缓存,不落库(本次与后续写入都跳过)
    if (p.inDb) {
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
        if (useReady && size <= 256) pix = th_impl::windowsShellThumb(filePath, size);
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
