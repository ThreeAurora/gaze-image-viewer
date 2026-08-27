#pragma once
// ═══════════════════════════════════════════════════════════
// WIC 色彩管理解码(仅用于 CMYK JPEG)
//
// 无嵌入 ICC 的 CMYK 印刷图,Qt/libjpeg 做"简单反演"(R=255-C 级别)
// 显示明显偏亮;Windows WIC 按系统默认印刷假定(SWOP 系)转换 sRGB,
// 与 QQ/Windows 照片应用同一条色彩管线,颜色沉实接近印刷意图。
//
// 触发条件由调用方保证:仅 4 通道(CMYK/YCCK) JPEG 调用本文件;
// 普通 RGB 图片一律走 Qt 原路径,行为零变化。
// 解码失败返回空 QImage,调用方回退 Qt 路径(绝不黑屏)。
//
// GUID 手动定义(不链接 windowscodecs.lib,CoCreateInstance 走 ole32
// 激活即可)——沿用 IID_IImageList 的先例,规避 MinGW GUID 符号缺失问题。
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QSize>
#include <QImage>
#include <QFile>
#include <qt_windows.h>
#include <wincodec.h>

namespace WicDecode {

// CLSID_WICImagingFactory {cacaf262-9370-4615-a13b-9f5539da4c0a}
static const GUID kCLSID_WICImagingFactory =
    {0xcacaf262, 0x9370, 0x4615, {0xa1, 0x3b, 0x9f, 0x55, 0x39, 0xda, 0x4c, 0x0a}};
// IID_IWICImagingFactory {ec5ec8a9-c395-4314-9c77-54d7a935ff70}
// ⚠️ 历史教训: 曾手写为 54d7a9-33ff4e(错),QueryInterface 永远 E_NOINTERFACE,
//    WIC 全程静默回退 Qt 路径(偏亮)——修复"修了但没修好"的根因。
static const GUID kIID_IWICImagingFactory =
    {0xec5ec8a9, 0xc395, 0x4314, {0x9c, 0x77, 0x54, 0xd7, 0xa9, 0x35, 0xff, 0x70}};

// 线程 COM 初始化作用域:构造初始化,析构按需配对释放。
// 已初始化为其它 apartment 模式(如主线程被 Qt 初始化为 STA)时不 uninit。
class ComScope {
public:
    ComScope() {
        HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_uninit = SUCCEEDED(hr);   // S_OK/S_FALSE → 配对 uninit;RPC_E_CHANGED_MODE → 不动
    }
    ~ComScope() { if (m_uninit) CoUninitialize(); }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
private:
    bool m_uninit = false;
};

// JPEG SOF 段解析:帧组件数 Nf==4 → CMYK/YCCK 印刷图。
// 只读文件头 64KB,SOI 之后顺序扫 marker,命中第一个 SOF 即返回。
inline bool isFourChannelJpeg(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    const QByteArray d = f.read(64 * 1024);
    if (d.size() < 4 || (uchar)d[0] != 0xFF || (uchar)d[1] != 0xD8) return false;
    int i = 2;
    const int n = d.size();
    while (i + 4 <= n) {
        if ((uchar)d[i] != 0xFF) { ++i; continue; }
        const uchar m = (uchar)d[i + 1];
        if (m == 0xFF) { ++i; continue; }                       // 填充字节
        if (m == 0x01 || (m >= 0xD0 && m <= 0xD7)) { i += 2; continue; }   // 无长度段
        const int len = ((uchar)d[i + 2] << 8) | (uchar)d[i + 3];
        if (len < 2) return false;
        const bool isSOF = (m >= 0xC0 && m <= 0xCF)
                           && m != 0xC4 && m != 0xC8 && m != 0xCC;
        if (isSOF) {
            if (i + 9 >= n) return false;
            const int nf = (uchar)d[i + 9];   // SOF: 长度(2) 精度(1) 高(2) 宽(2) Nf(1)
            return nf == 4;
        }
        i += 2 + len;
    }
    return false;
}

// CMYK JPEG → sRGB QImage(WIC 色彩管理管线)。
// want 非空且小于原图时经 IWICBitmapScaler 缩放输出(解码级,快),
// 否则输出原始尺寸。结果为 Format_ARGB32_Premultiplied。
inline QImage decodeCmyk(const QString& path, const QSize& want = QSize()) {
    ComScope com;   // 任意线程可调;未初始化 COM 的线程在此初始化

    IWICImagingFactory* factory = nullptr;
    if (FAILED(CoCreateInstance(kCLSID_WICImagingFactory, nullptr,
            CLSCTX_INPROC_SERVER, kIID_IWICImagingFactory, (void**)&factory)))
        return {};

    QImage result;
    IWICBitmapDecoder* decoder = nullptr;
    IWICBitmapFrameDecode* frame = nullptr;
    IWICBitmapScaler* scaler = nullptr;
    IWICFormatConverter* converter = nullptr;
    do {
        if (FAILED(factory->CreateDecoderFromFilename(
                (const wchar_t*)path.utf16(), nullptr, GENERIC_READ,
                WICDecodeMetadataCacheOnDemand, &decoder)))
            break;
        if (FAILED(decoder->GetFrame(0, &frame)))
            break;

        // 超大图防御(>100MP 交给 Qt 路径,防内存峰值失控)
        UINT w = 0, h = 0;
        frame->GetSize(&w, &h);
        if (w == 0 || h == 0 || qint64(w) * h > 100000000LL)
            break;

        // 可选解码级缩放:在 frame 与色彩转换之间插 scaler,
        // JPEG 小比例时 WIC 内部走原生抽点解码,几十 ms 出视口尺寸图
        IWICBitmapSource* source = frame;
        if (want.isValid() && want.width() > 0 && want.height() > 0
            && want.width() < int(w) && want.height() < int(h)) {
            if (SUCCEEDED(factory->CreateBitmapScaler(&scaler))
                && SUCCEEDED(scaler->Initialize(frame, UINT(want.width()),
                                UINT(want.height()),
                                WICBitmapInterpolationModeFant))) {
                source = scaler;
            }
        }

        // → 32bppPBGRA:这一步内部完成 CMYK→sRGB 色彩管理转换
        if (FAILED(factory->CreateFormatConverter(&converter)))
            break;
        if (FAILED(converter->Initialize(source, GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone, nullptr, 0.0,
                WICBitmapPaletteTypeMedianCut)))
            break;

        UINT cw = 0, ch = 0;
        converter->GetSize(&cw, &ch);
        if (cw == 0 || ch == 0) break;
        const int stride = int(cw) * 4;
        QByteArray buf(stride * int(ch), 0);
        if (FAILED(converter->CopyPixels(nullptr, UINT(stride), UINT(buf.size()),
                (BYTE*)buf.data())))
            break;
        QImage img((const uchar*)buf.constData(), int(cw), int(ch), stride,
                   QImage::Format_ARGB32_Premultiplied);
        result = img.copy();   // 拷贝脱离临时 buf
    } while (false);

    if (converter) converter->Release();
    if (scaler) scaler->Release();
    if (frame) frame->Release();
    if (decoder) decoder->Release();
    factory->Release();
    return result;
}

} // namespace WicDecode
