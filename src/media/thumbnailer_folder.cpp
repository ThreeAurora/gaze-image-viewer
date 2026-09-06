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
#include <QElapsedTimer>
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

// ── 本编译单元(#129 从 thumbnailer.cpp 拆出):文件夹卡片外框与四合一缩略图 ──

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
    QElapsedTimer ftClock;
    ftClock.start();

    // #139:候选只挑**本级**文件(图+视频)按自然序填格 —— 用户裁决:四合一
    // 不扫子目录补格(哪怕本级不足 4 张,空格露后板,也不去"借"子目录的图)。
    // 视频格走进程内 libav 单帧(videoThumbFFmpeg),不 spawn 外部 ffmpeg.exe;
    // 最坏 4 格全视频也只是 4 次解码,首生成付一次,之后命中 DB/内存缓存。
    // 单封面(folder4 关)维持原样:只取本级第一个候选。
    // #233:多备 4 个候选 —— "检测不到缩略图的图片/视频(0KB 等)不占格",
    // 解码失败就跳过、由后续候选顶上填格。只多挑不多解:前面的都成功时,
    // 备胎一个都不会被解码。
    const int want = p.folder4 ? 4 : 1;
    int videoCount = 0;
    QStringList picked;
    auto tryPick = [want, &picked, &videoCount](const QString& dir) {
        const int cap = want + 4;
        QDir sub(dir);
        auto raw = sub.entryInfoList(QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        QFileInfoList list = raw;
        std::stable_sort(list.begin(), list.end(), [](const QFileInfo& a, const QFileInfo& b) {
            return naturalNameLess(a.fileName(), b.fileName());
        });
        for (const QFileInfo& fi : list) {
            const QString ext = "." + fi.suffix().toLower();
            const bool isImg = IMAGE_EXTS.count(ext) > 0;
            const bool isVid = !isImg && VIDEO_EXTS.count(ext) > 0;
            if (!isImg && !isVid) continue;
            if (isVid) ++videoCount;
            picked << fi.absoluteFilePath();
            if (picked.size() >= cap) return true;
        }
        return false;
    };
    tryPick(dirPath);
    if (picked.isEmpty()) return {};

    QImage sheet(size, size, QImage::Format_RGB32);
    // 底色与"没有四合一的普通文件夹图标"一致:rgb(33,33,38)。此前用
    // C_CONTENT(近黑),四合一卡片四角/边缝露出的底和普通文件夹卡对不上
    sheet.fill(QColor(0x21, 0x21, 0x26));
    QPainter pt(&sheet);
    pt.setRenderHint(QPainter::Antialiasing);
    pt.setRenderHint(QPainter::SmoothPixmapTransform);

    const FolderFrame f = folderFrame(size);
    paintFolderBack(pt, f);

    // 单格:等比铺满后**居中**裁切(旧代码注释写着居中,实际从左上裁,横图看着偏)
    // #124 清晰度:格子只有卡片的 1/3 左右(206px 卡片 → 约 65px 格),以前是
    //   "按格子尺寸向 Windows Shell 要 65px" —— shell 给的是它自己二次缩放出来的
    //   小档软图,再画进格子就是糊的。现在一律**向原图**要一张"格子 2 倍、下限 256"
    //   的清晰图(与单图缩略图同一套 imageThumb 质量口径:highQuality 自带 2x 超采样),
    //   铺满格子后再由 painter 的 SmoothPixmapTransform 把 2x 降到 1x —— 整幅仍然
    //   全部可见,只是细节是真的。Shell 图退回作原图解码失败时的回退。
    //   代价:每格一次降采样解码(JPEG 走 libjpeg 的 DCT 缩放很便宜,PNG 是全解),
    //   只在生成时付一次,入库后不再有。
    auto drawCell = [this, &pt](const QString& path, const QRectF& cell) -> bool {
        const int cw = qMax(1, qRound(cell.width()));
        const int ch = qMax(1, qRound(cell.height()));
        const int res = qMax(256, 2 * qMax(cw, ch));
        QImage t;
        if (VIDEO_EXTS.count("." + QFileInfo(path).suffix().toLower())) {
            // 视频格:进程内 libav 单帧(取帧位置跟 Thumbs/videoFramePct 同源),
            // 内部失败时自走 fallback 链,这里不再叠加 shell 回退
            t = videoThumbFFmpeg(path, res);
        } else {
            t = imageThumb(path, res);
            if (t.isNull()) t = th_impl::windowsShellThumb(path, res);
        }
        if (t.isNull()) return false;   // #233:出不了图的候选不占格
        // 2x 超采样图上做居中裁切,画进 1x 格子 → 净效果是降采样,不放大不软
        const int sw = cw * 2, sh = ch * 2;
        t = t.scaled(sw, sh, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        pt.drawImage(cell, t, QRectF((t.width() - sw) / 2.0,
                                     (t.height() - sh) / 2.0, sw, sh));
        return true;
    };

    // 图裁进内容区(圆角)。#233(用户令:"子夹单缩略图只占左上小格不填满;
    // 检测不到缩略图的图片/视频(0KB等)不占四合一格"):
    //   · 四合一恒用 2×2 格 —— 只有 1 张时它坐左上小格,其余露后板,不再铺满;
    //   · 解码失败的候选不占格,由排在其后的候选顶上(备胎已在 picked 里,
    //     前面的都成功时备胎零解码)。
    // 单封面(folder4 关)维持铺满整块:第一个成功者胜出,失败顺延。
    pt.save();
    QPainterPath clip;
    clip.addRoundedRect(f.content, f.r, f.r);
    pt.setClipPath(clip);
    int cells = 0;
    if (want == 1) {
        for (const QString& path : picked)
            if (drawCell(path, f.content)) { cells = 1; break; }
    } else {
        // 格缝 2.5%(≥2px)才够 XnView 参考图那种"黄缝可见"——1% 时 160px 卡上只有 1px,看着像贴死的
        const qreal gap = qMax<qreal>(2.0, size * 0.025);
        const qreal cw = (f.content.width() - gap) / 2;
        const qreal ch = (f.content.height() - gap) / 2;
        for (int i = 0; i < picked.size() && cells < want; ++i) {
            const QRectF cell(f.content.left() + (cells % 2) * (cw + gap),
                              f.content.top()  + (cells / 2) * (ch + gap),
                              cw, ch);
            if (drawCell(picked[i], cell)) ++cells;
        }
    }
    pt.restore();

    pt.end();
    Logger::event(QStringLiteral("folderThumb: cells=%1/%2 video=%3 %4ms '%5'")
                      .arg(cells).arg(picked.size()).arg(videoCount)
                      .arg(ftClock.elapsed())
                      .arg(dirPath));
    return postProcess(sheet, size);
}
