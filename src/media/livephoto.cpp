#include "livephoto.h"

#include <QFileInfo>
#include <QDir>
#include <QFile>
#include <QDateTime>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTemporaryFile>
#include <QProcess>
#include <set>

namespace LivePhoto {

static const std::set<QString> MOTION_IMAGE_EXT = {
    ".heic", ".heif", ".jpg", ".jpeg", ".png"
};
static const std::set<QString> COMPANION_VIDEO_EXT = {
    ".mov", ".mp4", ".MOV", ".MP4"
};
// 常见 MP4 ftyp 品牌（4 字节标识）
static const std::set<QByteArray> MP4_BRANDS = {
    "isom", "iso2", "avc1", "mp41", "mp42", "MSNV", "mmp4",
    "3gp5", "3gp4", "3gp6", "qt  ", "M4V ", "M4A ",
    "F4V ", "F4P ", "F4A ", "F4B "
};

static const QByteArray JPEG_EOI("\xff\xd9", 2);
static const QByteArray MP4_FTYP("ftyp", 4);

// 动态照片元数据验证:同名 jpg+mp4 可能是独立文件(小红书/网页下载等),
// 必须图片内确有动态照片 XMP 标记才认 companion。
// 标记:Apple ContentType=3(EXIF/XMP) / Google MotionPhoto / Samsung MicroVideo
static bool hasMotionPhotoXmp(const QString& imagePath) {
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QByteArray head = f.read(256 * 1024);   // XMP 位于文件头部
    if (head.isEmpty()) return false;
    QByteArray low = head.toLower();
    if (low.contains("motionphoto")            // Google Motion Photo
        || low.contains("microvideo")          // Samsung
        || low.contains("photos/1.0/camera"))  // Google 相机命名空间
        return true;
    int idx = head.indexOf("ContentType");     // Apple Live Photo
    if (idx >= 0 && head.mid(idx, 64).contains('3'))
        return true;
    return false;
}

// ═══════════════════════════════════════════
// 统一入口
// ═══════════════════════════════════════════
std::optional<Info> detect(const QString& imagePath) {
    QFileInfo fi(imagePath);
    if (!fi.isFile()) return std::nullopt;
    if (!MOTION_IMAGE_EXT.count("." + fi.suffix().toLower())) return std::nullopt;

    auto r = detectCompanion(imagePath);
    if (r) return r;

    auto e = detectEmbedded(imagePath);
    if (e && e->videoOffset >= 0) return e;
    if (e) return e; // XMP-only, no offset

    return std::nullopt;
}

// ═══════════════════════════════════════════
// 外部配对视频（Apple Live Photo）
// ═══════════════════════════════════════════
std::optional<Info> detectCompanion(const QString& imagePath) {
    QFileInfo fi(imagePath);
    if (!MOTION_IMAGE_EXT.count("." + fi.suffix().toLower()))
        return std::nullopt;

    QStringList bases;
    bases << fi.completeBaseName();
    // Apple 命名规则：MVIMG_0001.jpg → IMG_0001.mov
    if (fi.completeBaseName().startsWith("MV"))
        bases << fi.completeBaseName().mid(2);

    QDir dir = fi.dir();
    for (const auto& base : bases) {
        for (const auto& ve : COMPANION_VIDEO_EXT) {
            QString vp = dir.filePath(base + ve);
            if (QFileInfo::exists(vp)) {
                // 同名视频存在 ≠ 动态照片:小红书/网页下载常有独立同名 jpg+mp4。
                // 必须图片内确有动态照片 XMP 标记(Apple/Google/Samsung)才认 companion
                if (!hasMotionPhotoXmp(imagePath)) continue;
                Info info;
                info.type = "companion";
                info.videoPath = vp;
                info.embedded = false;
                return info;
            }
        }
    }
    return std::nullopt;
}

// ═══════════════════════════════════════════
// JPEG 内嵌 MP4 检测（Google Motion Photo）
// ═══════════════════════════════════════════
std::optional<Info> detectEmbedded(const QString& imagePath) {
    QFileInfo fi(imagePath);
    if (!MOTION_IMAGE_EXT.count("." + fi.suffix().toLower()))
        return std::nullopt;

    qint64 fileSize = fi.size();
    if (fileSize < 100 * 1024) return std::nullopt;

    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) return std::nullopt;

    // 1. 搜索 JPEG EOI (FF D9) 最后出现的位置
    qint64 eoiOffset = -1;
    {
        qint64 chunk = 65536;
        f.seek(std::max(0LL, fileSize - chunk));
        QByteArray tail = f.read(chunk);

        int pos = tail.lastIndexOf(JPEG_EOI);
        if (pos < 0) {
            // 扩大范围至 5MB
            f.seek(std::max(0LL, fileSize - 5 * 1024 * 1024));
            tail = f.read(fileSize - f.pos());
            pos = tail.lastIndexOf(JPEG_EOI);
        }
        if (pos >= 0) {
            eoiOffset = fileSize - tail.size() + pos + 2;
        }
    }

    if (eoiOffset >= 0 && eoiOffset + 8 < fileSize) {
        f.seek(eoiOffset);
        QByteArray marker = f.read(12);
        if (marker.size() >= 12
            && marker.mid(4, 4) == MP4_FTYP
            && MP4_BRANDS.count(marker.mid(8, 4))) {
            Info info;
            info.type = "motion";
            info.videoPath = imagePath;
            info.videoOffset = eoiOffset;
            info.videoLength = fileSize - eoiOffset;
            info.embedded = true;
            return info;
        }
    }

    // 2. XMP MicroVideoOffset 定位（旧版 MVIMG，O(1)）
    //    官方语义：视频起点 = 文件大小 - offset；属性式 MicroVideoOffset="N" / 元素式 >N<
    f.seek(0);
    QByteArray head = f.read(131072);
    {
        QString headStr = QString::fromLatin1(head);
        int key = headStr.indexOf("MicroVideoOffset");
        if (key >= 0) {
            int i = key + 16; // strlen("MicroVideoOffset")
            const int n = headStr.size();
            while (i < n && !headStr.at(i).isDigit()) ++i;
            int j = i;
            while (j < n && headStr.at(j).isDigit()) ++j;
            if (j > i) {
                bool ok = false;
                qint64 offset = headStr.mid(i, j - i).toLongLong(&ok);
                if (ok && offset > 0 && offset < fileSize) {
                    qint64 videoStart = fileSize - offset;
                    f.seek(videoStart);
                    QByteArray marker = f.read(12);
                    if (marker.size() >= 12
                        && marker.mid(4, 4) == MP4_FTYP
                        && MP4_BRANDS.count(marker.mid(8, 4))) {
                        Info info;
                        info.type = "motion";
                        info.videoPath = imagePath;
                        info.videoOffset = videoStart;
                        info.videoLength = offset;
                        info.embedded = true;
                        return info;
                    }
                }
            }
        }
    }

    // 3. 回退：XMP 元数据确认（全文件扫描，代价高，最后手段）
    QString headStr = QString::fromUtf8(head);
    bool hasXmp = headStr.contains("MotionPhoto") ||
                  headStr.contains("MicroVideo") ||
                  headStr.contains("http://ns.google.com/photos/1.0/camera/");

    if (hasXmp) {
        // 全文件扫描 ftyp
        f.seek(0);
        QByteArray data = f.read(fileSize);
        int pos = -1;
        while (true) {
            pos = data.indexOf(MP4_FTYP, pos + 1);
            if (pos < 1024) continue; // 跳过图片头部的假 ftyp
            if (pos < 0) break;
            if (pos + 8 < data.size() && MP4_BRANDS.count(data.mid(pos + 4, 4))) {
                Info info;
                info.type = "motion";
                info.videoPath = imagePath;
                info.videoOffset = pos - 4;
                info.videoLength = fileSize - (pos - 4);
                info.embedded = true;
                return info;
            }
        }
        // 有 XMP 但找不到具体偏移
        Info info;
        info.type = "motion";
        info.videoPath = imagePath;
        info.embedded = true;
        return info;
    }

    return std::nullopt;
}

// ═══════════════════════════════════════════
// 提取内嵌视频到临时文件
// ═══════════════════════════════════════════
QString extractEmbeddedVideo(const QString& imagePath, const Info& info) {
    if (!info.embedded || info.videoOffset < 0) return {};

    QElapsedTimer sw;
    sw.start();
    QFile f(imagePath);
    if (!f.open(QIODevice::ReadOnly)) {
        Logger::event(QStringLiteral("ffmpeg-ext: open fail '%1'").arg(imagePath));
        return {};
    }

    f.seek(info.videoOffset);
    QByteArray data = f.read(info.videoLength);
    f.close();

    if (data.isEmpty()) {
        Logger::event("ffmpeg-ext: read empty");
        return {};
    }

    // 写入原始临时文件
    QTemporaryFile raw(QDir::tempPath() + "/xnn_live_XXXXXX.mp4");
    raw.setAutoRemove(false);
    if (!raw.open()) {
        Logger::event("ffmpeg-ext: temp write fail");
        return {};
    }
    QString rawPath = raw.fileName();
    raw.write(data);
    raw.close();

    // 用 ffmpeg 修复 MOOV atom（faststart）
    QTemporaryFile fixed(QDir::tempPath() + "/xnn_live_fixed_XXXXXX.mp4");
    fixed.setAutoRemove(false);
    if (!fixed.open()) return {};
    QString fixedPath = fixed.fileName();
    fixed.close();

    Logger::event(QStringLiteral("ffmpeg-ext: start len=%1 raw='%2'")
                      .arg(info.videoLength).arg(rawPath));
    const QString exe = locateFfmpegTool(QStringLiteral("ffmpeg"));
    if (exe.isEmpty()) {
        Logger::event(QStringLiteral("ffmpeg-ext: FALLBACK raw, ffmpeg 未找到(exe旁/PATH均无), %1 ms")
                          .arg(sw.elapsed()));
        return rawPath;
    }
    QProcess proc;
    hideConsoleWindow(proc);   // ffmpeg 修复动态照片同样不可闪控制台窗
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(exe, {
        "-i", rawPath, "-c", "copy", "-movflags", "faststart",
        "-y", fixedPath
    });

    if (proc.waitForFinished(10000) && proc.exitCode() == 0
        && QFileInfo::exists(fixedPath)
        && QFileInfo(fixedPath).size() > 0) {
        Logger::event(QStringLiteral("ffmpeg-ext: ok %1 ms exit=%2 fixed='%3' (%4 bytes)")
                          .arg(sw.elapsed()).arg(proc.exitCode())
                          .arg(fixedPath).arg(QFileInfo(fixedPath).size()));
        QFile::remove(rawPath);
        return fixedPath;
    }

    // ffmpeg 修复失败，返回原始提取文件
    const QString why = proc.state() == QProcess::Running
        ? QStringLiteral("timeout(>10s)")
        : QStringLiteral("exit=%1").arg(proc.exitCode());
    Logger::event(QStringLiteral("ffmpeg-ext: FALLBACK raw, %1, %2 ms")
                      .arg(why).arg(sw.elapsed()));
    QFile::remove(fixedPath);
    return rawPath;
}

} // namespace LivePhoto
