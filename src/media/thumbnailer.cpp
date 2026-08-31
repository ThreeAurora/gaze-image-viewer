#include "thumbnailer.h"
#include "constants.h"
#include "namesort.h"
#include "settings.h"
#include "wicdecode.h"
#include "settings.h"
#include "imgproc.h"
#include "wicdecode.h"

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
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头
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

    // 只裁不扩:个别路径(shell 大档位返回等)超出请求尺寸时裁回请求尺寸,
    // 缓存体积有界;绝不做二次降档(存小了放大必糊)
    if (size > 0 && (img.width() > size || img.height() > size))
        img = img.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);

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
// 文件夹卡片外框(XnView MP 同款):文件夹还是文件夹,内容图嵌在里面
//   几何比例与 fileentry.h::folderIcon 一致 —— 有图卡片与"目录内无图"回落的
//   纯图标因此是同一个轮廓,只差里面那几格图。
//   (#118 用户令删前板:浅黄横条挤占缩略图高度 —— 轮廓=tab+后板,内容吃满;
//    folderIcon 回落图原本就没有前板,无需同步)
//   底不透明(颜色=列表底色):Cache/compression 选 JPEG 时 alpha 会被压成黑底,
//   Thumbs/transparencyGrid 还会给它铺一层棋盘格,两者都会把外框毁成一坨
// ═══════════════════════════════════════════
namespace {

struct FolderFrame {
    QRectF tab;      // 左上凸出的标签
    QRectF back;     // 后板:内容图坐在它上面
    QRectF content;  // 内容图区
    qreal  r;        // 圆角
};

FolderFrame folderFrame(int size) {
    const qreal m = size * 0.03;
    FolderFrame f;
    f.r     = qMax<qreal>(1.0, size * 0.025);
    f.tab   = QRectF(m, size * 0.09, (size - 2 * m) * 0.42, size * 0.14);
    f.back  = QRectF(m, size * 0.18, size - 2 * m, size * 0.78);
    // #118:四边对称内缩 5% —— 底部不再给前板留 13.5%,缩略图吃满文件夹体
    f.content = f.back.adjusted(size * 0.05, size * 0.05,
                                -size * 0.05, -size * 0.05);
    // 极小尺寸(列表/详细 64px 以下)内缩可能吃掉内容区:保底留一半后板
    if (f.content.width() < f.back.width() * 0.5 || f.content.height() <= 2)
        f.content = f.back.adjusted(1, 1, -1, -f.back.height() * 0.05);
    return f;
}

void paintFolderBack(QPainter& pt, const FolderFrame& f) {
    pt.setPen(Qt::NoPen);
    QLinearGradient g(0, f.tab.top(), 0, f.back.bottom());
    g.setColorAt(0.0, QColor("#EFD98F"));
    g.setColorAt(1.0, QColor("#E0BC60"));
    pt.setBrush(g);
    pt.drawRoundedRect(f.tab, f.r, f.r);
    pt.drawRoundedRect(f.back, f.r, f.r);
}

} // namespace

QImage Thumbnailer::folderThumb(const QString& dirPath, int size) {
    const Prefs p = prefs();
    QDir d(dirPath);
    if (!d.exists()) return {};
    const auto raw = d.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
    // 挑选顺序必须与网格一致(#98):QDir::Name 是纯字典序(1, 10, 2),
    // 四合一会取到"1、10、2、3",和点开文件夹看到的前四张对不上
    QFileInfoList list = raw;
    std::stable_sort(list.begin(), list.end(), [](const QFileInfo& a, const QFileInfo& b) {
        return naturalNameLess(a.fileName(), b.fileName());
    });
    QStringList picked;
    const int want = p.folder4 ? 4 : 1;
    for (const QFileInfo& fi : list) {
        // 只挑图片:视频格要跑 ffmpeg 抽帧,一个目录几十个子目录时会拖慢浏览
        if (IMAGE_EXTS.count("." + fi.suffix().toLower()))
            picked << fi.absoluteFilePath();
        if (picked.size() >= want) break;
    }
    if (picked.isEmpty()) return {};

    QImage sheet(size, size, QImage::Format_RGB32);
    sheet.fill(QColor(C_CONTENT));
    QPainter pt(&sheet);
    pt.setRenderHint(QPainter::Antialiasing);
    pt.setRenderHint(QPainter::SmoothPixmapTransform);

    const FolderFrame f = folderFrame(size);
    paintFolderBack(pt, f);

    // 单格:等比铺满后**居中**裁切(旧代码注释写着居中,实际从左上裁,横图看着偏)
    auto drawCell = [this, &pt](const QString& path, const QRectF& cell) {
        const int cw = qMax(1, qRound(cell.width()));
        const int ch = qMax(1, qRound(cell.height()));
        QImage t = windowsShellThumb(path, qMax(cw, ch));
        if (t.isNull()) t = imageThumb(path, qMax(cw, ch));
        if (t.isNull()) return;
        t = t.scaled(cw, ch, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pt.drawImage(cell, t, QRectF((t.width() - cell.width()) / 2,
                                     (t.height() - cell.height()) / 2,
                                     cell.width(), cell.height()));
    };

    // 图裁进内容区(圆角) → 不足 4 张时空格露后板
    pt.save();
    QPainterPath clip;
    clip.addRoundedRect(f.content, f.r, f.r);
    pt.setClipPath(clip);
    const int n = qMin(picked.size(), want);
    if (n == 1) {
        drawCell(picked.first(), f.content);
    } else {
        // 格缝 2.5%(≥2px)才够 XnView 参考图那种"黄缝可见"——1% 时 160px 卡上只有 1px,看着像贴死的
        const qreal gap = qMax<qreal>(2.0, size * 0.025);
        const qreal cw = (f.content.width() - gap) / 2;
        const qreal ch = (f.content.height() - gap) / 2;
        for (int i = 0; i < n; ++i)
            drawCell(picked[i], QRectF(f.content.left() + (i % 2) * (cw + gap),
                                       f.content.top()  + (i / 2) * (ch + gap),
                                       cw, ch));
    }
    pt.restore();

    paintFolderFront(pt, f, size);
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
// 大库可能上千条,故只在后台线程跑一次,不阻塞启动;
// 逐 key 取 blob(内存峰值=单条缩略图),绝不 SELECT 全表 —— 库里可能躺着上百 MB 的图
void Thumbnailer::verifyCache() {
    const Prefs p = prefs();
    if (!p.useCatalog || !p.inDb) return;
    QThreadPool::globalInstance()->start([p]() {
        QSqlDatabase db = threadDb(p.dbCacheMB);
        if (!db.isOpen()) return;
        QStringList keys;
        {
            QSqlQuery q(db);
            if (!q.exec("SELECT key FROM thumbs")) return;
            while (q.next()) keys << q.value(0).toString();
        }
        if (keys.isEmpty()) return;
        QStringList bad;
        QSqlQuery one(db);
        one.prepare("SELECT png FROM thumbs WHERE key = ?");
        for (const QString& k : keys) {
            one.addBindValue(k);
            bool broken = true;
            if (one.exec() && one.next()) {
                const QByteArray blob = one.value(0).toByteArray();
                broken = blob.isEmpty() || QImage::fromData(blob).isNull();
            }
            if (broken) bad << k;
            one.finish();
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
