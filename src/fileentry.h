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
#include <unordered_map>
#include "constants.h"

namespace fs = std::filesystem;

// ═══════════════════════════════════════════
// FileEntry — 轻量级文件条目
// ═══════════════════════════════════════════
struct FileEntry {
    QString  name;
    QString  path;
    QString  ext;        // 小写后缀，含点号
    bool     isDir = false;
    int64_t  size  = 0;
    double   mtime = 0.0;
    double   ctime = 0.0;
    int      colorLabel = 0;  // 颜色标记:0无 1红 2橙 3黄 4绿 5蓝
};

// ═══════════════════════════════════════════
// 快速目录扫描 (std::filesystem)
// ═══════════════════════════════════════════
inline std::vector<FileEntry> fastScanDir(const QString& dirPath) {
    std::vector<FileEntry> entries;
    entries.reserve(500);

    std::error_code ec;
    auto dirPathW = dirPath.toStdWString();
    for (const auto& entry : fs::directory_iterator(dirPathW,
         fs::directory_options::skip_permission_denied, ec)) {
        if (ec) break;

        const auto& p = entry.path();
        auto fname = p.filename().wstring();
        if (!fname.empty() && fname[0] == L'.')
            continue; // 跳过隐藏文件/夹

        FileEntry fe;
        fe.name = QString::fromStdWString(fname);
        fe.path = QString::fromStdWString(p.wstring());
        fe.ext  = QString::fromStdWString(p.extension().wstring()).toLower();

        fe.isDir = entry.is_directory(ec);
        if (ec) { ec.clear(); continue; }

        if (!fe.isDir) {
            fe.size = entry.file_size(ec);
            if (ec) { ec.clear(); fe.size = 0; }
        }

        auto lwt = entry.last_write_time(ec);
        if (!ec) {
            auto sysTime = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
                lwt - fs::file_time_type::clock::now() + std::chrono::system_clock::now());
            fe.mtime = std::chrono::duration<double>(sysTime.time_since_epoch()).count();
        } else {
            ec.clear();
        }

        entries.push_back(std::move(fe));
    }
    return entries;
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
        {".heic","HEIC 图片"},{".heif","HEIF 图片"},
        {".tiff","TIFF 图片"},{".tif","TIFF 图片"},{".svg","SVG 图片"},
        {".mp4","MP4 视频"},{".mov","MOV 视频"},{".avi","AVI 视频"},
        {".mkv","MKV 视频"},{".webm","WebM 视频"},{".wmv","WMV 视频"},
        {".flv","FLV 视频"},{".mp3","MP3 音频"},{".wav","WAV 音频"},
        {".flac","FLAC 音频"},{".aac","AAC 音频"},{".ogg","OGG 音频"},
        {".m4a","M4A 音频"},{".zip","ZIP 压缩"},{".rar","RAR 压缩"},
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
    cache[filePath] = sz;
    return sz;
}

// ═══════════════════════════════════════════
// 图标生成
// ═══════════════════════════════════════════

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif
// 系统图标提取需要 Vista+ API(SHGetImageList/SHIL_JUMBO 256px)
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

// 文件夹图标:橙黄渐变铺满整个缩略图框(经典双板文件夹造型)
inline QIcon folderIcon(int size) {
    static std::unordered_map<int, QIcon> cache;
    auto it = cache.find(size);
    if (it != cache.end()) return it->second;

    QPixmap pix(size, size);
    pix.fill(Qt::transparent);
    QPainter p(&pix);
    p.setRenderHint(QPainter::Antialiasing);

    qreal m = size * 0.04;                       // 四周 4% 边距 → 铺满
    QRectF body(m, size * 0.22, size - 2 * m, size * 0.72);
    QRectF tab(m, size * 0.12, (size - 2 * m) * 0.46, size * 0.16);

    // 后板(深一档)
    p.setPen(QPen(QColor("#8A5E10"), size * 0.012));
    p.setBrush(QColor("#C8871C"));
    p.drawRoundedRect(body, size * 0.03, size * 0.03);

    // 前板:橙黄线性渐变(顶亮底深)
    QLinearGradient grad(0, body.top(), 0, body.bottom());
    grad.setColorAt(0.0, QColor("#FFD75E"));
    grad.setColorAt(0.55, QColor("#F5B93B"));
    grad.setColorAt(1.0, QColor("#E09520"));
    p.setBrush(grad);
    p.drawRoundedRect(body.adjusted(0, size * 0.07, 0, 0), size * 0.03, size * 0.03);

    // 标签凸起
    p.setBrush(QColor("#F5B93B"));
    p.drawRoundedRect(tab, size * 0.025, size * 0.025);

    // 顶部高光线
    p.setPen(QPen(QColor(255, 255, 255, 70), size * 0.008));
    p.setBrush(Qt::NoBrush);
    p.drawLine(QPointF(body.left() + size * 0.03, body.top() + size * 0.09),
               QPointF(body.right() - size * 0.03, body.top() + size * 0.09));
    p.end();

    cache[size] = QIcon(pix);
    return cache[size];
}

// 文件类型图标:走 Windows 系统关联(SHGetFileInfo)
//   .db→数据库图标、exe→自带图标、无关联→空白折纸,与资源管理器一致
//   filePath 非空且存在 → 按真实文件取图标(如 bootmgr 等无扩展名但有专属图标的文件)
//   高清优先:真实文件 PrivateExtractIcons 256px → SHDefExtractIcon 请求 256 → 32px 回退
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

    auto takeBig = [&ic](HICON h) -> bool {
        if (!h) return false;
        QImage img = hiconToQImage(h);
        DestroyIcon(h);
        if (!img.isNull()) { ic = QIcon(QPixmap::fromImage(img)); return true; }
        return false;
    };

    // 1) 真实文件自带图标资源(exe/dll/ico/scr 等):直接抽 256px
    if (useReal) {
        HICON h = nullptr;
        if (PrivateExtractIconsW(w.c_str(), 0, 256, 256, &h, nullptr, 1, 0) >= 1)
            takeBig(h);
    }
    // 2) 按关联解析请求 256px(对不存在路径也按扩展名给图标)
    if (ic.isNull()) {
        HICON h = nullptr;
        if (SUCCEEDED(SHDefExtractIconW(w.c_str(), 0, 0, &h, nullptr,
                                        MAKELONG(256, 256))) && h)
            takeBig(h);
    }
    // 3) 回退:32px 大图标
    if (ic.isNull()) {
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(w.c_str(), attrs, &sfi,
                           sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON | SHGFI_USEFILEATTRIBUTES)
            && sfi.hIcon) {
            QImage img = hiconToQImage(sfi.hIcon);
            if (!img.isNull()) ic = QIcon(QPixmap::fromImage(img));
            DestroyIcon(sfi.hIcon);
        }
    }
    if (ic.isNull())
        ic = QApplication::style()->standardIcon(QStyle::SP_FileIcon);

    cache[cacheKey] = ic;
    return ic;
}
