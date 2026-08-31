#pragma once
#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QColor>
#include <QFileInfo>
#include <QDateTime>
#include <QImageReader>
#include <QApplication>
#include <QStyle>
#include <vector>
#include <string>
#include <filesystem>
#include <chrono>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include "constants.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
// 系统图标/目录枚举需要 Vista+ API(SHGetImageList/SHIL_JUMBO)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>        // ILD_TRANSPARENT
#include <shobjidl.h>
#include <commoncontrols.h>  // IImageList 接口 + IID_IImageList(GUID 声明,uuid 库给定义)

#ifndef NOMINMAX
#define NOMINMAX
#endif
// 系统图标/目录枚举需要 Vista+ API(SHGetImageList/SHIL_JUMBO)
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06000000
#endif
#ifndef _WIN32_IE
#define _WIN32_IE 0x0600
#endif
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <commctrl.h>        // ILD_TRANSPARENT
#include <shobjidl.h>
#include <commoncontrols.h>  // IImageList 接口 + IID_IImageList(GUID 声明,uuid 库给定义)

namespace fs = std::filesystem;

// ═══════════════════════════════════════════
// FileEntry — 轻量级文件条目
// ═══════════════════════════════════════════
struct FileEntry {
    QString  name;
    QString  path;
    QString  ext;        // 小写后缀，含点号
    bool     isDir = false;
    bool     hidden = false; // 隐藏文件/文件夹（Windows 隐藏属性或点开头）
    int64_t  size  = 0;
    double   mtime = 0.0;
    double   ctime = 0.0;
    int      colorLabel = 0;  // 颜色标记:0无 1红 2橙 3黄 4绿 5蓝
};

// ═══════════════════════════════════════════
// 快速目录扫描 (FindFirstFileW:一次内核调用同时取全
//   创建时间/修改时间/大小/属性 —— std::filesystem 拿不到创建时间,
//   二次逐文件取属性又违背大目录快速扫描的初衷)
// ═══════════════════════════════════════════
inline double fileTimeToEpoch(const FILETIME& ft) {
    ULARGE_INTEGER u;
    u.HighPart = ft.dwHighDateTime;
    u.LowPart = ft.dwLowDateTime;
    if (u.QuadPart == 0) return 0.0;
    // 100ns since 1601-01-01 → 秒 since 1970-01-01
    return double(u.QuadPart / 10000000ull) - 11644473600.0;
}

// 一条 FindNextFile 记录 → FileEntry(单层扫描与递归扫描共用,避免两处口径漂移)
inline FileEntry entryFromFindData(const WIN32_FIND_DATAW& data, const QString& dirPath) {
    FileEntry fe;
    fe.name = QString::fromWCharArray(data.cFileName);
    fe.path = dirPath + QLatin1Char('/') + fe.name;
    const int dot = fe.name.lastIndexOf(QLatin1Char('.'));
    fe.ext = (dot > 0) ? fe.name.mid(dot).toLower() : QString();
    // Windows 隐藏属性 / 点开头文件/夹都算隐藏,显示时用淡灰色
    fe.hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
                || fe.name.startsWith(QLatin1Char('.'));
    fe.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    fe.ctime = fileTimeToEpoch(data.ftCreationTime);
    fe.mtime = fileTimeToEpoch(data.ftLastWriteTime);
    if (!fe.isDir)
        fe.size = (int64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return fe;
}

// 一条 FindNextFile 记录 → FileEntry(单层扫描与递归扫描共用,避免两处口径漂移)
inline FileEntry entryFromFindData(const WIN32_FIND_DATAW& data, const QString& dirPath) {
    FileEntry fe;
    fe.name = QString::fromWCharArray(data.cFileName);
    fe.path = dirPath + QLatin1Char('/') + fe.name;
    const int dot = fe.name.lastIndexOf(QLatin1Char('.'));
    fe.ext = (dot > 0) ? fe.name.mid(dot).toLower() : QString();
    // Windows 隐藏属性 / 点开头文件/夹都算隐藏,显示时用淡灰色
    fe.hidden = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
                || fe.name.startsWith(QLatin1Char('.'));
    fe.isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    fe.ctime = fileTimeToEpoch(data.ftCreationTime);
    fe.mtime = fileTimeToEpoch(data.ftLastWriteTime);
    if (!fe.isDir)
        fe.size = (int64_t(data.nFileSizeHigh) << 32) | data.nFileSizeLow;
    return fe;
}

inline std::vector<FileEntry> fastScanDir(const QString& dirPath) {
    std::vector<FileEntry> entries;
    entries.reserve(500);

    const QString pattern = dirPath + QStringLiteral("\\*");
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileExW((const wchar_t*)pattern.utf16(),
                                FindExInfoBasic, &data,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return entries;

    do {
        const wchar_t* fname = data.cFileName;
        // directory_iterator 语义:跳过 "." 与 ".."
        if (fname[0] == L'.' && (fname[1] == 0 || (fname[1] == L'.' && fname[2] == 0)))
            continue;
        entries.push_back(entryFromFindData(data, dirPath));
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return entries;
}

// ═══════════════════════════════════════════
// 递归收集"子文件夹里的文件"(文件夹树右键:显示子文件夹中的文件)
//   目录行仍按单层给出,只有文件向下递归展开 —— 与 XnView 同形。
//   代价上限:深度 64 层 / 累计 20 万条,超出即止(宁可少显示,不可卡死界面)。
//   重解析点(junction、符号链接目录)一律不进:跟着走会成环,是这类递归的经典事故。
// ═══════════════════════════════════════════
inline void fastScanSubFiles(const QString& dirPath, std::vector<FileEntry>& out,
                             bool skipHiddenDirs = false,
                             int depth = 0, size_t limit = 200000) {
    if (depth > 64 || out.size() >= limit) return;

    const QString pattern = dirPath + QStringLiteral("\\*");
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileExW((const wchar_t*)pattern.utf16(),
                                FindExInfoBasic, &data,
                                FindExSearchNameMatch, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return;

    QStringList subs;
    do {
        const wchar_t* fname = data.cFileName;
        if (fname[0] == L'.' && (fname[1] == 0 || (fname[1] == L'.' && fname[2] == 0)))
            continue;
        const bool isDir = (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
        if (!isDir) {
            out.push_back(entryFromFindData(data, dirPath));
            if (out.size() >= limit) break;
        } else if (!(data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
            // 不跟随重解析点(junction/符号链接目录):跟着走会成环,是这类递归的经典事故
            const QString name = QString::fromWCharArray(fname);
            const bool hiddenDir = (data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN)
                                   || name.startsWith(QLatin1Char('.'));
            if (!(skipHiddenDirs && hiddenDir))
                subs << dirPath + QLatin1Char('/') + name;
        }
    } while (FindNextFileW(h, &data));
    FindClose(h);

    for (const QString& d : subs)
        fastScanSubFiles(d, out, skipHiddenDirs, depth + 1, limit);
}

// ═══════════════════════════════════════════
// 文件头嗅探(FileList/recognizeByExt = 关 时启用)
//   只读前 64 字节比对魔数,判不出来返回空(调用方保留原扩展名)
//   默认"只按扩展名识别"开着时这个函数一次都不会被调用
// ═══════════════════════════════════════════
inline QString sniffExtByHeader(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    const QByteArray h = f.read(64);
    f.close();
    if (h.size() < 12) return {};

    auto has = [&](int off, const char* sig, int n) {
        return h.size() >= off + n && std::memcmp(h.constData() + off, sig, n) == 0;
    };

    if (has(0, "\xFF\xD8\xFF", 3))                       return ".jpg";
    if (has(0, "\x89PNG\r\n\x1A\n", 8))                  return ".png";
    if (has(0, "GIF8", 4))                               return ".gif";
    if (has(0, "BM", 2))                                 return ".bmp";
    if (has(0, "\x49\x49\x2A\x00", 4)
        || has(0, "\x4D\x4D\x00\x2A", 4))                return ".tif";
    if (has(0, "\x00\x00\x01\x00", 4))                   return ".ico";
    if (has(0, "8BPS", 4))                               return ".psd";
    if (has(0, "%PDF", 4))                               return ".pdf";
    if (has(0, "Rar!", 4))                               return ".rar";
    if (has(0, "7z\xBC\xAF\x27\x1C", 6))                 return ".7z";
    if (has(0, "\x1F\x8B", 2))                           return ".gz";
    if (has(0, "PK\x03\x04", 4))                         return ".zip";
    if (has(0, "fLaC", 4))                               return ".flac";
    if (has(0, "OggS", 4))                               return ".ogg";
    if (has(0, "ID3", 3))                                return ".mp3";
    if (has(0, "\xFF\xFB", 2) || has(0, "\xFF\xF3", 2)
        || has(0, "\xFF\xF2", 2))                        return ".mp3";
    if (has(0, "MZ", 2))                                 return ".exe";
    // RIFF 家族:偏移 8 起的四字符决定具体类型
    if (has(0, "RIFF", 4)) {
        if (has(8, "WEBP", 4)) return ".webp";
        if (has(8, "WAVE", 4)) return ".wav";
        if (has(8, "AVI ", 4)) return ".avi";
        return {};
    }
    // ISO-BMFF 家族(mp4/mov/heic/avif):偏移 4 是 "ftyp"
    if (has(4, "ftyp", 4)) {
        if (has(8, "heic", 4) || has(8, "heix", 4)
            || has(8, "mif1", 4) || has(8, "msf1", 4))   return ".heic";
        // avif/avis(序列)必须排在 mp4 兜底前:裸 avif 的 ftyp 也会落到这
        if (has(8, "avif", 4) || has(8, "avis", 4))      return ".avif";
        if (has(8, "qt  ", 4))                           return ".mov";
        if (has(8, "M4V ", 4))                           return ".m4v";
        return ".mp4";
    }
    // JPEG XL 两种封装:裸码流 \xFF\x0A 与 ISO-BMFF 容器
    if (has(0, "\xFF\x0A", 2))                           return ".jxl";
    if (has(0, "\x00\x00\x00\x0C\x4A\x58\x4C\x20", 8))   return ".jxl";
    return {};
}

// ═══════════════════════════════════════════
// 格式化工具
// ═══════════════════════════════════════════
inline QString formatSize(int64_t num) {
    if (num < 0) num = 0;
    double n = static_cast<double>(num);
    const char* units[] = {"B", "KB", "MB", "GB", "TB"};
    int idx = 0;
    while (n >= 1024.0 && idx < 4) { n /= 1024.0; ++idx; }
    if (idx == 0)
        return QString::number(static_cast<int>(n)) + " " + units[idx];
    return QString::asprintf("%.2f %s", n, units[idx]);
}

inline QString formatDate(double timestamp) {
    if (timestamp <= 0) return {};
    auto t = static_cast<time_t>(timestamp);
    return QDateTime::fromSecsSinceEpoch(t).toString("yyyy/MM/dd - HH:mm:ss");
}

inline QString mimeType(const QString& ext) {
    static const std::unordered_map<QString, QString> map = {
        {".jpg","JPEG 图片"},{".jpeg","JPEG 图片"},{".png","PNG 图片"},
        {".gif","GIF 图片"},{".bmp","BMP 图片"},{".webp","WebP 图片"},
        {".heic","HEIC 图片"},{".heif","HEIF 图片"},{".hif","HEIF 图片"},
        {".avif","AVIF 图片"},{".avifs","AVIF 序列"},{".jxl","JPEG XL 图片"},
        {".tiff","TIFF 图片"},{".tif","TIFF 图片"},{".svg","SVG 图片"},
        {".exr","EXR 图片"},{".dds","DDS 图片"},{".qoi","QOI 图片"},
        {".jp2","JPEG 2000 图片"},{".dpx","DPX 图片"},{".apng","APNG 图片"},
        {".mp4","MP4 视频"},{".mov","MOV 视频"},{".avi","AVI 视频"},
        {".mkv","MKV 视频"},{".webm","WebM 视频"},{".wmv","WMV 视频"},
        {".flv","FLV 视频"},{".3g2","3G2 视频"},{".ogm","OGM 视频"},
        {".m1v","MPEG-1 视频"},{".m2v","MPEG-2 视频"},
        {".mp3","MP3 音频"},{".wav","WAV 音频"},
        {".flac","FLAC 音频"},{".aac","AAC 音频"},{".ogg","OGG 音频"},
        {".m4a","M4A 音频"},{".wma","WMA 音频"},{".opus","OPUS 音频"},
        {".amr","AMR 音频"},{".ac3","AC3 音频"},{".wma","WMA 音频"},{".opus","OPUS 音频"},
        {".amr","AMR 音频"},{".ac3","AC3 音频"},{".zip","ZIP 压缩"},{".rar","RAR 压缩"},
        {".7z","7Z 压缩"},{".txt","文本文档"},{".md","Markdown"},
        {".py","Python"},{".js","JavaScript"},{".pdf","PDF 文档"},
        {".doc","Word 文档"},{".docx","Word 文档"},{".xlsx","Excel 表格"},
        {".exe","应用程序"},{".dll","动态链接库"},
    };
    auto it = map.find(ext);
    if (it != map.end()) return it->second;
    return ext.mid(1).toUpper() + " 文件";
}

// ═══════════════════════════════════════════
// 图片尺寸查询（从 Python 移植）
// ═══════════════════════════════════════════
inline QSize imageSize(const QString& filePath) {
    static std::unordered_map<QString, QSize> cache;
    auto it = cache.find(filePath);
    if (it != cache.end()) return it->second;
    QImageReader reader(filePath);
    QSize sz = reader.size();
    // 封顶:进程内跨目录累积不清理会一路涨。上限远大于任一正常单目录,
    // 单次 sort 不中途清空(避免比较器反复读盘),仅长期逛很多目录后回收
    if (cache.size() > 50000) cache.clear();
    cache[filePath] = sz;
    return sz;
}

// ═══════════════════════════════════════════
// 图标生成
// ═══════════════════════════════════════════

// HICON → QImage(Qt6 无 QPixmap::fromHICON,用 GetDIBits 手工转换)
inline QImage hiconToQImage(HICON hIcon) {
    ICONINFO ii = {};
    if (!GetIconInfo(hIcon, &ii)) return {};
    QImage result;
    if (ii.hbmColor) {
        BITMAP bm = {};
        if (GetObjectW(ii.hbmColor, sizeof(bm), &bm) && bm.bmWidth > 0) {
            BITMAPINFOHEADER bi = {};
            bi.biSize = sizeof(bi);
            bi.biWidth = bm.bmWidth;
            bi.biHeight = -bm.bmHeight;   // top-down
            bi.biPlanes = 1;
            bi.biBitCount = 32;
            bi.biCompression = BI_RGB;
            QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
            HDC hdc = GetDC(NULL);
            GetDIBits(hdc, ii.hbmColor, 0, bm.bmHeight, img.bits(),
                      reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
            ReleaseDC(NULL, hdc);
            result = img;
        }
        DeleteObject(ii.hbmColor);
    }
    if (ii.hbmMask) DeleteObject(ii.hbmMask);
    return result;
}

// 文件夹图标:浅奶油黄、柔和扁平、铺满缩略图框(对照 XnView MP 样式)
inline QIcon folderIcon(int size) {
    static std::unordered_map<int, QIcon> cache;
    auto it = cache.find(size);
    if (it != cache.end()) return it->second;

    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);

    qreal m = size * 0.03;                       // 铺满:四周仅 3% 边距
    QRectF body(m, size * 0.18, size - 2 * m, size * 0.78);
    QRectF tab(m, size * 0.09, (size - 2 * m) * 0.42, size * 0.12);

    // 主体:浅奶油黄,极轻的上下渐变(柔和,无描边)
    QLinearGradient grad(0, tab.top(), 0, body.bottom());
    grad.setColorAt(0.0, QColor("#F6E3A2"));
    grad.setColorAt(0.35, QColor("#F3D98C"));
    grad.setColorAt(1.0, QColor("#EBC96E"));
    p.setBrush(grad);
    p.drawRoundedRect(tab, size * 0.02, size * 0.02);
    p.drawRoundedRect(body, size * 0.025, size * 0.025);

    // #131:原来这里还画一条"稍深的收边"(body 底部 4.5% 高、#DFB95C)。#118 把
    //   四合一卡片里的前板横条删掉了,但**目录里没有图片时走的是这个回落图标**,
    //   那条带子还在 —— 用户看到的"底下那道颜色不一样的黄条"就是它。删。
    p.end();

    cache[size] = QIcon(pix);
    return cache[size];
}

// 文件类型图标:走 Windows 系统关联(SHGetImageList 多档 16/32/48/256)
//   多分辨率 QIcon → Qt 按显示尺寸自动选最近档,消除单源放大模糊
//   filePath 非空且存在 → 按真实文件取图标(如 bootmgr 等无扩展名但有专属图标的文件)
inline QIcon typeIcon(const QString& ext, const QString& filePath = QString()) {
    static std::unordered_map<QString, QIcon> cache;
    QString cacheKey = filePath.isEmpty() ? ext : filePath;
    auto it = cache.find(cacheKey);
    if (it != cache.end()) return it->second;

    QIcon ic;
    QFileInfo rfi(filePath);
    bool useReal = !filePath.isEmpty() && rfi.exists();
    QString srcPath = useReal ? filePath : QStringLiteral("gaze_probe") + ext;
    std::wstring w = srcPath.toStdWString();
    DWORD attrs = useReal ? 0 : FILE_ATTRIBUTE_NORMAL;

    // 从 HICON 提取 QImage 并入 QIcon(多分辨率档位);内部负责 DestroyIcon
    auto takeIcon = [&ic](HICON h) -> bool {
        if (!h) return false;
        QImage img = hiconToQImage(h);
        DestroyIcon(h);
        if (img.isNull()) return false;
        ic.addPixmap(QPixmap::fromImage(img));
        return true;
    };

    // 1) 真实文件自带图标资源(exe/dll/ico 等):直接抽 256px(最高清)
    if (useReal) {
        HICON h = nullptr;
        if (PrivateExtractIconsW(w.c_str(), 0, 256, 256, &h, nullptr, 1, 0) >= 1)
            takeIcon(h);
    }

    // 2) 系统关联图标:SHGetImageList 一次取 256/48/32/16 四档(高清优先)
    {
        // IID_IImageList 规范值:MinGW 头仅声明、uuid 库未导出符号,
        // 手动定义等值 GUID(SHGetImageList 按值匹配),避免链接依赖
        static const GUID iidImageList = { 0x46eb5926, 0x582e, 0x4017,
            { 0x9f, 0xdf, 0xe8, 0x99, 0x8d, 0xaa, 0x09, 0x50 } };
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(w.c_str(), attrs, &sfi, sizeof(sfi),
                           SHGFI_SYSICONINDEX | SHGFI_USEFILEATTRIBUTES)
            && sfi.iIcon >= 0) {
            const int ilModes[] = { SHIL_JUMBO, SHIL_EXTRALARGE, SHIL_LARGE, SHIL_SMALL };
            for (int m : ilModes) {
                IImageList* il = nullptr;
                if (SUCCEEDED(SHGetImageList(m, iidImageList, (void**)&il)) && il) {
                    HICON h = nullptr;
                    if (SUCCEEDED(il->GetIcon(sfi.iIcon, ILD_TRANSPARENT, &h)) && h)
                        takeIcon(h);
                    il->Release();
                }
            }
        }
    }

    // 3) 兜底:SHDefExtractIconW 请求 256px(对拿不到 SYSICONINDEX 的扩展名)
    if (ic.isNull()) {
        HICON h = nullptr;
        if (SUCCEEDED(SHDefExtractIconW(w.c_str(), 0, 0, &h, nullptr,
                                        MAKELONG(256, 256))) && h)
            takeIcon(h);
    }

    // 4) 最终回退:QStyle 标准图标(自带多尺寸)
    if (ic.isNull())
        ic = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    cache[cacheKey] = ic;
    return ic;
}
