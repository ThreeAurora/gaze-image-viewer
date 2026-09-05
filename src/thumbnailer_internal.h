#pragma once

// ═══════════════════════════════════════════
// thumbnailer.cpp 拆分(#129)后的跨编译单元共享实现件
//   这里只放"被两个以上 .cpp 用到"的东西;单 TU 私有的 helper
//   (video: ffmpegExe / probeDurationSec 及其 memo / seekMsForPct,
//    folder: FolderFrame / folderFrame / paintFolderBack)
//   原样留在各自的 .cpp,不进本头。
//   搬移本身零行为改动,唯一必要的形态变化是 linkage:
//     static -> inline(函数) / 函数局部 static 引用访问器(带状态的数据),
//   以保持"全程序单实例 + 每线程一份连接"的原有语义。
// ═══════════════════════════════════════════
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>

#include <QString>
#include <QImage>
#include <QThread>
#include <QPainter>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QMutex>
#include <atomic>

#include "settings.h"

namespace th_impl {

// ═══════════════════════════════════════════
// 每线程独立 SQLite 连接
//   QSqlDatabase 连接只能在创建线程使用；线程池 worker 各自持有连接，
//   靠 WAL 模式支持多连接并发——修复 qsqlite.dll AV 崩溃（跨线程共用 m_db）
// ═══════════════════════════════════════════
inline QMutex& dbCreateMutex() {
    // 原来是文件级 static 数据:搬进头文件后每个 TU 会各生成一份,
    // 锁就不再互斥。改成返回函数局部 static 引用的 inline 访问器,全程序仍是一份。
    static QMutex s_dbCreateMutex;
    return s_dbCreateMutex;
}

// Cache/dbCacheMB → PRAGMA cache_size(负值=KB)。连接按线程各一份,故每线程记住
// 已应用值:设置改动后,该线程下次用到连接时自动重设,无需重启
inline void applyDbCache(const QSqlDatabase& db, int mb) {
    static thread_local int applied = -1;
    if (applied == mb) return;
    QSqlQuery q(db);
    q.exec(QString("PRAGMA cache_size=-%1").arg(mb * 1024));
    applied = mb;
}

inline QSqlDatabase threadDb(int cacheMB) {
    const QString connName = QStringLiteral("thumb_") + QString::number(
        reinterpret_cast<quintptr>(QThread::currentThreadId()), 16);

    if (!QSqlDatabase::contains(connName)) {
        QMutexLocker lk(&dbCreateMutex());
        // 双检：并发首建时避免重复 add 同名连接
        if (!QSqlDatabase::contains(connName)) {
            QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE", connName);
            db.setDatabaseName(
                AppSettings::instance().dataDir() + "/thumbnails.db");
            if (db.open()) {
                QSqlQuery q(db);
                q.exec("PRAGMA journal_mode=WAL");
                q.exec("PRAGMA synchronous=NORMAL");
                q.exec("CREATE TABLE IF NOT EXISTS thumbs "
                       "(key TEXT PRIMARY KEY, png BLOB, mtime REAL, atime REAL DEFAULT 0)");
                q.exec("CREATE INDEX IF NOT EXISTS idx_atime ON thumbs(atime)");
                // #244 文件夹大小缓存库:路径→精确总大小+失效键+统计时刻。
                // 失效键=目录 mtime|直接子项数|直接子项字节和(读写双方都用
                // 同一算法算,不用建索引——主键 path 就是唯一入口)
                q.exec("CREATE TABLE IF NOT EXISTS dirsize "
                       "(path TEXT PRIMARY KEY, size INTEGER, basis TEXT, computed INTEGER)");
                // 盘根连体键迁移(一次性):旧版条目路径在盘根是 "G://x" 形
                // (fastScanDir 对规范形 "G:/" 再补分隔符的连锁),整棵根下
                // 子树都带连体首分隔符;不迁,升级后缩略图/文件夹大小缓存全
                // 部未命中白重算。起始双斜杠是 UNC 头,不动;同文件新旧两形态
                // 都在时 UPDATE OR REPLACE 合并。进程级 atomic 保证只跑一次
                static std::atomic<bool> uniSlashDone{false};
                if (!uniSlashDone.exchange(true)) {
                    q.exec("UPDATE OR REPLACE thumbs SET key = REPLACE(key, '//', '/') "
                           "WHERE substr(key,1,2) <> '//' AND key LIKE '%//%'");
                    q.exec("UPDATE OR REPLACE dirsize SET path = REPLACE(path, '//', '/') "
                           "WHERE substr(path,1,2) <> '//' AND path LIKE '%//%'");
                }
            }
        }
    }
    QSqlDatabase db = QSqlDatabase::database(connName);
    if (db.isOpen()) applyDbCache(db, cacheMB);
    return db;
}

// ═══════════════════════════════════════════
// Windows Shell 缩略图缓存（Everything 1.5a 同款方案）
//   直接读 Windows thumbcache_*.db，无需解码
//   图片视频通吃，通常是 0-5ms 级别
// ═══════════════════════════════════════════
inline QImage windowsShellThumb(const QString& filePath, int size) {
    // 每个线程独立初始化 COM（QThreadPool 线程默认未初始化）
    HRESULT coHr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
    bool needUninit = SUCCEEDED(coHr) && coHr != S_FALSE;

    IShellItemImageFactory* factory = nullptr;
    std::wstring wpath = filePath.toStdWString();
    HRESULT hr = SHCreateItemFromParsingName(wpath.c_str(), nullptr,
                                              IID_PPV_ARGS(&factory));
    if (FAILED(hr) || !factory) {
        if (needUninit) CoUninitialize();
        return {};
    }

    HBITMAP hbmp = nullptr;
    SIZE sz = {size, size};
    hr = factory->GetImage(sz, SIIGBF_RESIZETOFIT, &hbmp);
    factory->Release();
    if (FAILED(hr) || !hbmp) {
        if (needUninit) CoUninitialize();
        return {};
    }

    // HBITMAP → QPixmap
    BITMAP bm;
    GetObjectW(hbmp, sizeof(BITMAP), &bm);
    BITMAPINFOHEADER bi = {};
    bi.biSize = sizeof(BITMAPINFOHEADER);
    bi.biWidth = bm.bmWidth;
    bi.biHeight = -bm.bmHeight;
    bi.biPlanes = 1;
    bi.biBitCount = 32;
    bi.biCompression = BI_RGB;

    QImage img(bm.bmWidth, bm.bmHeight, QImage::Format_ARGB32);
    HDC hdc = GetDC(NULL);
    GetDIBits(hdc, hbmp, 0, bm.bmHeight, img.bits(),
              reinterpret_cast<BITMAPINFO*>(&bi), DIB_RGB_COLORS);
    ReleaseDC(NULL, hdc);
    DeleteObject(hbmp);

    if (needUninit) CoUninitialize();
    return img;
}

// #242:exe/ico 一类的 shell"缩略图"其实就是图标本身。个别 exe 的图标资源档位
// 小,shell 会把小图标画在请求画布的左上角、其余全透明 —— 入库后显示成
// "左上角一小块"。把不透明内容的边界框裁出来:已基本铺满画布(真缩略图,
// 或轻微透明留白)原样返回;只有明显偏居一隅的才按原大小居中回贴
// (图标再放大只会糊,不采用放缩填满)。
inline QImage trimPadCenter(QImage img) {
    if (img.isNull() || img.format() != QImage::Format_ARGB32
        || !img.hasAlphaChannel())
        return img;
    int minX = img.width(), minY = img.height(), maxX = -1, maxY = -1;
    for (int y = 0; y < img.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(img.constScanLine(y));
        for (int x = 0; x < img.width(); ++x) {
            if (qAlpha(line[x]) > 8) {
                if (x < minX) minX = x;
                if (x > maxX) maxX = x;
                if (y < minY) minY = y;
                if (y > maxY) maxY = y;
            }
        }
    }
    if (maxX < 0) return img;                       // 整幅全透明,原样交给后处理
    const int cw = maxX - minX + 1, ch = maxY - minY + 1;
    // 内容已贴边或铺满 ≥75% 线性尺寸:真缩略图/贴边构图,不动
    if (minX == 0 && minY == 0
        && cw * 4 >= img.width() * 3 && ch * 4 >= img.height() * 3)
        return img;
    QImage out(img.size(), QImage::Format_ARGB32);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.drawImage((img.width() - cw) / 2, (img.height() - ch) / 2,
                img.copy(minX, minY, cw, ch));
    p.end();
    return out;
}

} // namespace th_impl
