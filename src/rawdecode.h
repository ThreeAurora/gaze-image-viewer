#pragma once
// ═══════════════════════════════════════════════════════════
// RAW 全解(rawdecode):自带 LibRaw(thirdparty/LibRaw 静态编入 gaze)
//
// 全囊括铁令(用户 2026-09-01):一切功能不默认用户电脑装有任何组件,
// RAW 解码必须随程序自带 —— 与 vendor/gs(PDF)、vendor/ffmpeg 同一架构
// 原则;LibRaw 是纯源码(LGPL-2.1 / CDDL 双许可),静态编进 exe,无 DLL
// 分发决策。去马赛克/白平衡/色彩矩阵都在 LibRaw 内置(dcraw 管线 +
// 机身色彩数据),gaze 不自造色彩科学。
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

} // namespace RawDecode

#endif // HAS_RAWDEC
