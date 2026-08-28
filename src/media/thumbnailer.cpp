#include "thumbnailer.h"
#include "constants.h"
#include "settings.h"
#include "wicdecode.h"
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
    p.sharpen   = st.get("Thumbs/sharpen", true).toBool();
    p.gamma     = st.get("Thumbs/gamma", false).toBool();
    // ── 设置→缓存数据库 ──
    p.useCatalog   = st.get("Cache/useCatalog", true).toBool();
    p.thumbW       = qBound(64, st.get("Cache/thumbWidth", 465).toInt(), 1024);
    p.thumbH       = qBound(64, st.get("Cache/thumbHeight", 365).toInt(), 1024);
    p.checkStartup = st.get("Cache/checkOnStartup", false).toBool();
    QMutexLocker lk(&m_prefMutex);
    m_prefs = p;
}

Thumbnailer::Prefs Thumbnailer::prefs() const {
    QMutexLocker lk(&m_prefMutex);
    return m_prefs;
}

// ═══════════════════════════════════════════
// 缩略图后处理(设置→缩略图→处理)
//   Thumbs/alpha         关 → 压平为不透明(背景=透明网格或纯色)
//   Thumbs/transparencyGrid 开 → 透明处画 8px 棋盘格(结果同样不透明)
//   Thumbs/sharpen       开 → 3x3 轻度锐化(仅对缩略图尺寸,开销可忽略)
// ═══════════════════════════════════════════
static QImage checkerBg(int w, int h) {
    QImage bg(w, h, QImage::Format_RGB32);
    const int cell = 8;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const bool odd = ((x / cell) + (y / cell)) & 1;
            bg.setPixel(x, y, odd ? 0xFF3A3A40 : 0xFF26262B);
        }
    }
    return bg;
}

static QImage sharpenImage(const QImage& src) {
    // 核 [0 -0.5 0; -0.5 3 -0.5; 0 -0.5 0](和为 1,亮度守恒)
    QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage dst(s.size(), QImage::Format_ARGB32);
    const int w = s.width(), h = s.height();
    const double k[3][3] = {{0, -0.5, 0}, {-0.5, 3.0, -0.5}, {0, -0.5, 0}};
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            double r = 0, g = 0, b = 0, a = 0;
            for (int dy = -1; dy <= 1; ++dy) {
                const int yy = qBound(0, y + dy, h - 1);
                for (int dx = -1; dx <= 1; ++dx) {
                    const int xx = qBound(0, x + dx, w - 1);
                    const QRgb px = s.pixel(xx, yy);
                    const double kv = k[dy + 1][dx + 1];
                    r += qRed(px)   * kv;
                    g += qGreen(px) * kv;
                    b += qBlue(px)  * kv;
                    a += qAlpha(px) * kv;
                }
            }
            const auto cl = [](double v) { return static_cast<int>(qBound(0.0, v, 255.0)); };
            dst.setPixel(x, y, qRgba(cl(r), cl(g), cl(b), cl(a)));
        }
    }
    return dst;
}

// 线性光降采样(Thumbs/gamma):先 sRGB→linear 再盒式平均,最后 linear→sRGB。
// 直接在 gamma 空间做平均会让亮部偏暗/暗部偏亮(经典降采样失真)
static QImage linearDownscale(const QImage& src, const QSize& dst) {
    static double s2l[256], l2s[1024];
    static bool built = false;
    if (!built) {
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            s2l[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }
        for (int i = 0; i < 1024; ++i) {
            const double c = i / 1023.0;
            l2s[i] = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        }
        built = true;
    }
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage out(dst, QImage::Format_ARGB32);
    if (out.isNull()) return src.scaled(dst, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    const double rx = double(s.width())  / dst.width();
    const double ry = double(s.height()) / dst.height();
    for (int y = 0; y < dst.height(); ++y) {
        for (int x = 0; x < dst.width(); ++x) {
            const int x0 = int(x * rx), x1 = qMin(s.width(),  int((x + 1) * rx));
            const int y0 = int(y * ry), y1 = qMin(s.height(), int((y + 1) * ry));
            double lr = 0, lg = 0, lb = 0, la = 0;
            int n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    const QRgb px = s.pixel(xx, yy);
                    const double av = qAlpha(px) / 255.0;
                    lr += s2l[qRed(px)]   * av;
                    lg += s2l[qGreen(px)] * av;
                    lb += s2l[qBlue(px)]  * av;
                    la += av;
                    ++n;
                }
            }
            if (n == 0 || la <= 0) { out.setPixel(x, y, 0); continue; }
            const auto enc = [](double lin) {
                const int idx = qBound(0, int(lin * 1023.0 + 0.5), 1023);
                return qBound(0, int(l2s[idx] * 255.0 + 0.5), 255);
            };
            out.setPixel(x, y, qRgba(enc(lr / la), enc(lg / la), enc(lb / la),
                                     qBound(0, int(la / n * 255.0 + 0.5), 255)));
        }
    }
    return out;
}

QImage Thumbnailer::postProcess(QImage img, int size) const {
    if (img.isNull()) return img;
    const Prefs p = prefs();

    // 尺寸包围盒(Cache/thumbWidth × Cache/thumbHeight):只裁不扩,
    // 默认 465x365 大于任何卡片缩略图,故默认情况下行为与设置前完全一致
    const int cap = qMin(p.thumbW, p.thumbH);
    if (cap > 0 && (img.width() > cap || img.height() > cap))
        img = img.scaled(cap, cap, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    Q_UNUSED(size);

    // Thumbs/gamma:线性光降采样(已在 imageThumb 内按此开关处理,这里只兜底裁剪)
    // ── 透明处理 ──
    const bool hasAlpha = img.hasAlphaChannel();
    if (hasAlpha && !p.alpha) {
        QImage flat(img.size(), QImage::Format_RGB32);
        flat.fill(p.transGrid ? 0xFF000000 : 0xFF2A2A2E);
        if (p.transGrid) {
            QImage bg = checkerBg(img.width(), img.height());
            QPainter pt(&flat);
            pt.drawImage(0, 0, bg);
            pt.drawImage(0, 0, img);
            pt.end();
        } else {
            QPainter pt(&flat);
            pt.drawImage(0, 0, img);
            pt.end();
        }
        img = flat;
    } else if (hasAlpha && p.transGrid) {
        QImage flat = checkerBg(img.width(), img.height());
        QPainter pt(&flat);
        pt.drawImage(0, 0, img);
        pt.end();
        img = flat;
    }

    // Thumbs/sharpen
    if (p.sharpen) img = sharpenImage(img);
    return img;
}

// ═══════════════════════════════════════════
// 文件夹缩略图(Thumbs/folder4)
//   开 → 2x2 拼前 4 张图;关 → 只取第一张做封面
//   目录内无可用图片则返回空(交由调用方显示系统文件夹图标)
// ═══════════════════════════════════════════
QImage Thumbnailer::folderThumb(const QString& dirPath, int size) {
    const Prefs p = prefs();
    QDir d(dirPath);
    if (!d.exists()) return {};
    const auto list = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    QStringList picked;
    const int want = p.folder4 ? 4 : 1;
    for (const QFileInfo& fi : list) {
        const QString ext = "." + fi.suffix().toLower();
        if (IMAGE_EXTS.count(ext) || VIDEO_EXTS.count(ext)) picked << fi.absoluteFilePath();
        if (picked.size() >= want) break;
    }
    if (picked.isEmpty()) return {};

    if (!p.folder4) return postProcess(imageThumb(picked.first(), size), size);

    // 2x2 拼图:每格留 2px 间隙,格内等比裁切居中(与系统文件夹缩略图观感一致)
    const int gap = 2;
    const int cell = (size - gap) / 2;
    QImage sheet(size, size, QImage::Format_RGB32);
    sheet.fill(0xFF1E1E22);
    QPainter pt(&sheet);
    for (int i = 0; i < picked.size() && i < 4; ++i) {
        QImage t = imageThumb(picked[i], cell);
        if (t.isNull()) t = windowsShellThumb(picked[i], cell);
        if (t.isNull()) continue;
        t = t.scaled(cell, cell, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = (i % 2) * (cell + gap);
        const int oy = (i / 2) * (cell + gap);
        pt.drawImage(ox, oy, t.copy(0, 0, qMin(cell, t.width()), qMin(cell, t.height())));
    }
    pt.end();
    return postProcess(sheet, size);
}

// ═══════════════════════════════════════════
// 视频四帧拼图(Thumbs/video4)
//   从 Thumbs/videoFramePct 指定的位置起,在剩余时长内均匀取 4 帧
// ═══════════════════════════════════════════
QImage Thumbnailer::videoContactSheet(const QString& filePath, int size) {
    const int start = prefs().framePct;
    const int gap = 2;
    const int cell = (size - gap) / 2;
    QImage sheet(size, size, QImage::Format_RGB32);
    sheet.fill(0xFF000000);
    QPainter pt(&sheet);
    int drawn = 0;
    for (int i = 0; i < 4; ++i) {
        const int pct = start + (100 - start) * i / 4;
        QImage f = videoThumbFFmpeg(filePath, cell, pct);
        if (f.isNull()) continue;
        f = f.scaled(cell, cell, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = (i % 2) * (cell + gap);
        const int oy = (i / 2) * (cell + gap);
        pt.drawImage(ox, oy, f.copy(0, 0, qMin(cell, f.width()), qMin(cell, f.height())));
        ++drawn;
    }
    pt.end();
    return drawn ? postProcess(sheet, size) : QImage();
}

// 缓存完整性校验(Cache/checkOnStartup):逐条尝试解码,读不出来的条目删除。
// 大库可能上千条,故只在后台线程跑一次,不阻塞启动
void Thumbnailer::verifyCache() {
    const Prefs p = prefs();
    if (!p.useCatalog || !p.inDb) return;
    QThreadPool::globalInstance()->start([p]() {
        QSqlDatabase db = threadDb(p.dbCacheMB);
        if (!db.isOpen()) return;
        QStringList bad;
        QSqlQuery q(db);
        if (q.exec("SELECT key, png FROM thumbs")) {
            while (q.next()) {
                const QByteArray blob = q.value(1).toByteArray();
                if (blob.isEmpty() || QImage::fromData(blob).isNull())
                    bad << q.value(0).toString();
            }
        }
        if (bad.isEmpty()) return;
        db.transaction();
        QSqlQuery del(db);
        del.prepare("DELETE FROM thumbs WHERE key = ?");
        for (const QString& k : bad) { del.addBindValue(k); del.exec(); }
        db.commit();
    });
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

    // Cache/thumbWidth × Cache/thumbHeight:缓存缩略图的包围盒上限。
    // 只裁不扩,默认 465x365 > 任何卡片缩略图,故默认情况下与改造前一致
    const Prefs p0 = prefs();
    const int cap = qMin(p0.thumbW, p0.thumbH);
    if (cap > 0 && size > cap) size = cap;

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
