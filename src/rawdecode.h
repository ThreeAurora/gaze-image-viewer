#pragma once
// ═══════════════════════════════════════════════════════════
// RAW 全解(rawdecode):自带 LibRaw(thirdparty/LibRaw 静态编入 Gaze)
//
// 全囊括铁令(用户 2026-09-01):一切功能不默认用户电脑装有任何组件,
// RAW 解码必须随程序自带 —— 与 vendor/gs(PDF)、vendor/ffmpeg 同一架构
// 原则;LibRaw 是纯源码(LGPL-2.1 / CDDL 双许可),静态编进 exe,无 DLL
// 分发决策。去马赛克/白平衡/色彩矩阵都在 LibRaw 内置(dcraw 管线 +
// 机身色彩数据),Gaze 不自造色彩科学。
//
// 只服务「加载原始RAW」按钮(#140):全解是几十 MP 数秒级的重活,绝不进
// 缩略图/预读管线。解码失败返回空 QImage,调用方显示失败文案(绝不黑屏)。
// HAS_RAWDEC 未定义(CMake 没找到 thirdparty/LibRaw)时本头整体为空,
// 调用方走"未编入解码器"提示。
// ═══════════════════════════════════════════════════════════

#ifdef HAS_RAWDEC

#include <QString>
#include <QImage>
#include <libraw/libraw.h>

namespace RawDecode {

// 全流程:open_file → unpack → dcraw_process(去马赛克+白平衡+sRGB)→ 位图。
// 白平衡用机身拍摄时定下的(use_camera_wb):用户对"相机回放观感"最熟悉;
// 其余保持 dcraw 默认:sRGB 输出、AHD 去马赛克、自动亮度、按元数据翻转。
// 任意线程可调(LibRaw 实例自足,无共享状态);调用方保证在线程池里跑。
inline QImage decodeFull(const QString& path) {
    LibRaw r;
    r.imgdata.params.use_camera_wb = 1;
#if defined(_WIN32)
    if (r.open_file((const wchar_t*)path.utf16()) != LIBRAW_SUCCESS) return {};
#else
    if (r.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS) return {};
#endif
    if (r.unpack() != LIBRAW_SUCCESS) return {};
    if (r.dcraw_process() != LIBRAW_SUCCESS) return {};

    int err = LIBRAW_UNSPECIFIED_ERROR;
    libraw_processed_image_t* im = r.dcraw_make_mem_image(&err);
    if (!im || im->type != LIBRAW_IMAGE_BITMAP || im->bits != 8) {
        if (im) LibRaw::dcraw_clear_mem(im);
        return {};
    }
    // mem_image 位图无行对齐填充:行距 = 宽 × 通道数
    const int stride = im->width * im->colors;
    QImage out(im->data, im->width, im->height, stride,
               im->colors == 3 ? QImage::Format_RGB888
                               : QImage::Format_Grayscale8);
    out = out.copy();   // 拷贝脱离 LibRaw 缓冲
    LibRaw::dcraw_clear_mem(im);
    return out;
}

// 提取相机内嵌的完整 JPEG 预览(2026-09-02 用户令:RAW 默认预览图就是它)。
// CR2/NEF/ARW 等在文件头都带一张机身生成的预览 JPG(有的还带 embeddable
// 大图),取它 = 与相机回放所见一致,且几乎零成本 —— 不必全解几十 MP。
// maxSide>0 时等比降采样(缩略图/预览共用)。失败返回空 QImage,调用方
// 回退到「加载原始 RAW」按钮/占位。
inline QImage decodeEmbeddedJpeg(const QString& path, int maxSide = 0) {
    LibRaw r;
#if defined(_WIN32)
    if (r.open_file((const wchar_t*)path.utf16()) != LIBRAW_SUCCESS) return {};
#else
    if (r.open_file(path.toLocal8Bit().constData()) != LIBRAW_SUCCESS) return {};
#endif
    // unpack_thumb() 只读内嵌缩略图,不碰主图,快;返回值=缩略图类型
    const int tret = r.unpack_thumb();
    if (tret != LIBRAW_SUCCESS) return {};
    const libraw_thumbnail_t& th = r.imgdata.thumbnail;
    if (!th.thumb) return {};
    QImage out;
    if (th.tformat == LIBRAW_THUMBNAIL_JPEG) {
        out.loadFromData(reinterpret_cast<const uchar*>(th.thumb),
                         static_cast<int>(th.tlength), "JPG");
    } else if (th.tformat == LIBRAW_THUMBNAIL_BITMAP) {
        // 少数机型给 BMP 序列:按位图头解析出宽高直接贴
        out = QImage::fromData(QByteArray(
            reinterpret_cast<const char*>(th.thumb),
            static_cast<int>(th.tlength)));
        if (!out.isNull() && out.format() != QImage::Format_RGB32)
            out = out.convertToFormat(QImage::Format_RGB32);
    }
    if (out.isNull()) return {};
    if (maxSide > 0 && qMax(out.width(), out.height()) > maxSide)
        out = out.scaled(maxSide, maxSide, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
    return out;
}

} // namespace RawDecode

#endif // HAS_RAWDEC
