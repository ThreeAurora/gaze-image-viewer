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

// ── 本编译单元(#129 从 thumbnailer.cpp 拆出):后处理 / 缓存 key / SQLite 读写与校验与淘汰 / blob 编码 ──

// ═══════════════════════════════════════════
// 缩略图后处理(设置→缩略图→处理)
//   Thumbs/alpha         关 → 压平为不透明(背景=透明网格或纯色)
//   Thumbs/transparencyGrid 开 → 透明处画 8px 棋盘格(结果同样不透明)
//   Thumbs/sharpen       开 → 3x3 轻度锐化(仅对缩略图尺寸,开销可忽略)
// ═══════════════════════════════════════════
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
            QImage bg = ImgProc::checkerBg(img.width(), img.height());
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
        QImage flat = ImgProc::checkerBg(img.width(), img.height());
        QPainter pt(&flat);
        pt.drawImage(0, 0, img);
        pt.end();
        img = flat;
    }

    // Thumbs/sharpen:默认关。要开也只用 0.5——1.0 的强反卷积在缩略图上
    // 出边缘光晕/失真,还会放大 WebP 量化伪影(用户实测反馈"高度锐化失真")
    if (p.sharpen) img = ImgProc::sharpen(img, 0.5);
    return img;
}

// 缓存完整性校验(Cache/checkOnStartup):逐条尝试解码,读不出来的条目删除。
// 大库可能上千条,故只在后台线程跑一次,不阻塞启动;
// 逐 key 取 blob(内存峰值=单条缩略图),绝不 SELECT 全表 —— 库里可能躺着上百 MB 的图
void Thumbnailer::verifyCache() {
    const Prefs p = prefs();
    if (!p.useCatalog || !p.inDb) return;
    QThreadPool::globalInstance()->start([p]() {
        QSqlDatabase db = th_impl::threadDb(p.dbCacheMB);
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
            bool broken = true;
            one.addBindValue(k);
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
// 缓存 Key
// ═══════════════════════════════════════════
QString Thumbnailer::cacheKey(const QString& filePath, int size, bool isVideo) const {
    // v2 + 锐化位:旧代条目烤着"1.0 强锐化 + WebP q75"的伪影,换 key 整体弃用,
    // 随容量淘汰自然清掉;锐化开关进 key,设置切换后缩略图真的重生成而非吃旧缓存
    const Prefs p = prefs();
    QString src = filePath + '|' + QString::number(size)
                + "|v2s" + (p.sharpen ? '1' : '0');
    // c1 = CMYK JPG 换代(#57):旧条目烤着 Qt/libjpeg 简单反演(偏亮)的图;
    // 换 key 使已有缓存整体弃用,重生成走 WIC 色彩管理。只探测 jpg/jpeg,
    // 其它后缀免一次文件头读取(非 JPG 会因魔数不符在 isFourChannelJpeg 内快速失败)
    const QString suf = QFileInfo(filePath).suffix().toLower();
    if (suf == "jpg" || suf == "jpeg") {
        if (WicDecode::isFourChannelJpeg(filePath)) src += "|c1";
    }
    // i2 = 图标型文件换代(#242):旧条目可能烤着"小图标贴画布左上角"的 shell
    // 原样,换代后经 trimPadCenter 居中重生成
    if (suf == "exe" || suf == "dll" || suf == "ico" || suf == "scr"
        || suf == "msi" || suf == "cpl" || suf == "lnk" || suf == "ocx")
        src += "|i2";
    // f2 = 文件夹外框代际:旧条目是全幅 2x2 拼图,不改 key 就永远读不到新样式。
    // f3 = #118 那一代:删掉前板浅黄横条、内容区吃满文件夹体。
    // f4 = #124 那一代:四格改成"向原图要 2 倍/≥256px 再降采样",旧条目烤着
    //      shell 小档的软图,不换代际用户看到的还是糊的那张。
    // f5 = #139 那一代:四合一不再扫子目录补格,只取本级文件。旧 f4 条目里可能
    //      "借"了子目录的图,用户裁决不符预期,换代强制重生成。
    //      都只给目录换 key —— 图片/视频的 blob 与这些改版无关,不该陪它们重新解码
    // f6 = #233 那一代:四合一恒 2×2(单图坐左上小格不铺满)+解码失败候选不占格
    //      由备胎顶上 —— 旧条目烤着"单图铺满整块/失败候选占格"的旧布局,不换 key
    //      用户看到的还是旧样式
    // f7 = 画布底色换代:C_CONTENT 近黑改 rgb(33,33,38),与普通文件夹卡底一致;
    //      旧条目四角烤着黑底,不换代看不出来
    if (QFileInfo(filePath).isDir()) src += "|f7";
    // 视频:取帧位置与四帧拼图决定画面内容,但不吃 key 的话改设置只影响新生成的条目,
    // 老库里永远是旧那一帧 —— 看起来就像设置没接线(#106 那批死设置的同一种病)。
    // 只在取非默认值时追加:默认(pct=0/单帧)与既有库逐字节一致,不改设置的人
    // 不必为这次接线重解一遍全库
    if (isVideo) {
        if (p.framePct > 0) src += "|fp" + QString::number(p.framePct);
        if (p.video4)       src += "|v4";
    }
    return QString::fromLatin1(
        QCryptographicHash::hash(src.toUtf8(), QCryptographicHash::Md5).toHex());
}

// ═══════════════════════════════════════════
// 内存 LRU 缓存
// ═══════════════════════════════════════════
bool Thumbnailer::cacheLookup(const QString& key, double mtime, QImage& out) {
    QMutexLocker lk(&m_cacheMutex);
    auto it = m_memCache.find(key);
    if (it != m_memCache.end() && std::abs(it->second.mtime - mtime) < 0.001) {
        it->second.lastAccess = std::chrono::steady_clock::now().time_since_epoch().count();
        out = it->second.pixmap;
        return true;
    }
    return false;
}

void Thumbnailer::cacheStore(const QString& key, const QImage& pix, double mtime) {
    const Prefs p = prefs();

    // 写入 SQLite（异步无害，这里同步写；连接为本线程专属）
    if (p.inDb) {
        QByteArray blob = pixmapToBlob(pix);
        if (!blob.isEmpty()) {
            QSqlDatabase db = th_impl::threadDb(p.dbCacheMB);
            if (!db.isOpen()) return;
            QSqlQuery q(db);
            q.prepare("INSERT OR REPLACE INTO thumbs VALUES (?,?,?,?)");
            q.addBindValue(QVariant(key));
            q.addBindValue(QVariant(blob));
            q.addBindValue(QVariant(mtime));
            q.addBindValue(QVariant(static_cast<double>(
                std::chrono::system_clock::now().time_since_epoch().count())));
            q.exec();
            evictIfNeeded();
        }
    }

    // 写入内存缓存
    {
        QMutexLocker lk(&m_cacheMutex);
        int64_t pixBytes = static_cast<int64_t>(pix.width()) * pix.height() * 4;

        // LRU 淘汰
        while (m_memCacheBytes + pixBytes > MAX_MEM_CACHE && !m_memCache.empty()) {
            auto oldest = m_memCache.begin();
            for (auto it = m_memCache.begin(); it != m_memCache.end(); ++it) {
                if (it->second.lastAccess < oldest->second.lastAccess)
                    oldest = it;
            }
            int64_t oldBytes = static_cast<int64_t>(oldest->second.pixmap.width())
                             * oldest->second.pixmap.height() * 4;
            m_memCacheBytes -= oldBytes;
            m_memCache.erase(oldest);
        }

        CacheEntry entry;
        entry.pixmap = pix;
        entry.mtime = mtime;
        entry.lastAccess = std::chrono::steady_clock::now().time_since_epoch().count();
        m_memCache[key] = entry;
        m_memCacheBytes += pixBytes;
    }
}

// ═══════════════════════════════════════════
// Pixmap → blob(编码按 Cache/compression:0 无 / 1 无损 ZIP / 2 有损高品质 JPEG
//   / 3 低品质 JPEG / 4 WebP q90 = 默认。此前 q75 在平滑渐变上出色带条纹,
//   提到 q90 后肉眼无可辨伪影,体积增幅可忽略)
//   读取端 QImage::fromData 自动识别格式,库内混合格式条目仍可读
// ═══════════════════════════════════════════
QByteArray Thumbnailer::pixmapToBlob(const QImage& pix) const {
    const char* codec = "webp";
    int quality = 90;
    switch (prefs().blobCodec) {
    case 0: case 1: codec = "png";  quality = -1;  break;
    case 2:         codec = "jpg";  quality = 95;  break;
    case 3:         codec = "jpg";  quality = 70;  break;
    default:        codec = "webp"; quality = 90;  break;
    }
    QByteArray ba;
    QBuffer buf(&ba);
    buf.open(QIODevice::WriteOnly);
    if (quality < 0) pix.save(&buf, codec);
    else             pix.save(&buf, codec, quality);
    return ba;
}

// ═══════════════════════════════════════════
// 缓存逐出（DB 超过 Cache/maxCacheMB 时淘汰最旧 20%;Cache/maxCacheOn=关 则不淘汰）
// ═══════════════════════════════════════════
void Thumbnailer::evictIfNeeded() {
    const Prefs p = prefs();
    if (!p.capOn) return;

    QSqlDatabase db = th_impl::threadDb(p.dbCacheMB);
    if (!db.isOpen()) return;

    QFileInfo fi(db.databaseName());
    if (!fi.exists()) return;

    qint64 dbSize = fi.size() / (1024 * 1024);
    if (dbSize <= p.maxDbMB) return;

    QSqlQuery q(db);
    q.exec("SELECT COUNT(*) FROM thumbs");
    int total = 0;
    if (q.next()) total = q.value(0).toInt();

    if (total > 0) {
        int delN = std::max(1, total / 5);
        q.prepare("DELETE FROM thumbs WHERE key IN "
                   "(SELECT key FROM thumbs ORDER BY atime ASC LIMIT ?)");
        q.addBindValue(QVariant(delN));
        q.exec();
        QSqlQuery("VACUUM", db).exec();
    }
}
