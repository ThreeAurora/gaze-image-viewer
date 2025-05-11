#pragma once
#include <QImage>
#include <QPainter>
#include <QColor>
#include <QSize>
#include <cmath>
#include <cstring>

// ═══════════════════════════════════════════
// 图像后处理公共件(缩略图管线 + 查看器渲染共用)
//   之前 linearResample/sharpen/checkerBg 在 thumbnailer.cpp 和
//   previewpanel.cpp 各写了一份,已经出现参数漂移 —— 收口到这里。
// ═══════════════════════════════════════════
namespace ImgProc {

// sRGB ↔ 线性光查找表(进程内只建一次)
struct GammaLut {
    double s2l[256];
    double l2s[1024];
    GammaLut() {
        for (int i = 0; i < 256; ++i) {
            const double c = i / 255.0;
            s2l[i] = c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
        }
        for (int i = 0; i < 1024; ++i) {
            const double c = i / 1023.0;
            l2s[i] = c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
        }
    }
};

inline const GammaLut& lut() {
    static const GammaLut g;
    return g;
}

// 线性光重采样:先 sRGB→linear 再盒式平均,最后 linear→sRGB。
// 直接在 gamma 空间做插值,亮部/暗部边界会失真(降采样发灰、放大发暗)
inline QImage linearResample(const QImage& src, const QSize& dstSize) {
    if (src.isNull() || dstSize.isEmpty()) return {};
    const QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage out(dstSize, QImage::Format_ARGB32);
    if (out.isNull()) return {};
    const GammaLut& g = lut();
    const double rx = double(s.width())  / dstSize.width();
    const double ry = double(s.height()) / dstSize.height();
    for (int y = 0; y < dstSize.height(); ++y) {
        for (int x = 0; x < dstSize.width(); ++x) {
            const int x0 = int(x * rx), x1 = qMin(s.width(),  int((x + 1) * rx));
            const int y0 = int(y * ry), y1 = qMin(s.height(), int((y + 1) * ry));
            double lr = 0, lg = 0, lb = 0, la = 0;
            int n = 0;
            for (int yy = y0; yy < y1; ++yy) {
                for (int xx = x0; xx < x1; ++xx) {
                    const QRgb px = s.pixel(xx, yy);
                    const double av = qAlpha(px) / 255.0;
                    lr += g.s2l[qRed(px)]   * av;
                    lg += g.s2l[qGreen(px)] * av;
                    lb += g.s2l[qBlue(px)]  * av;
                    la += av;
                    ++n;
                }
            }
            if (n == 0 || la <= 0) { out.setPixel(x, y, 0); continue; }
            const auto enc = [&g](double lin) {
                const int idx = qBound(0, int(lin * 1023.0 + 0.5), 1023);
                return qBound(0, int(g.l2s[idx] * 255.0 + 0.5), 255);
            };
            out.setPixel(x, y, qRgba(enc(lr / la), enc(lg / la), enc(lb / la),
                                     qBound(0, int(la / n * 255.0 + 0.5), 255)));
        }
    }
    return out;
}

// 3x3 锐化卷积。amount=锐化强度:0.5 ≈ XnView"使用锐化 50%",
// 1.0 ≈ 缩略图管线的标准强度(核和恒为 1,亮度不漂移)
inline QImage sharpen(const QImage& src, double amount) {
    if (src.isNull()) return {};
    QImage s = src.convertToFormat(QImage::Format_ARGB32);
    QImage dst(s.size(), QImage::Format_ARGB32);
    const int w = s.width(), h = s.height();
    const double c = 1.0 + 4.0 * amount;      // 中心系数
    const double n = -amount;                 // 邻域系数
    const double k[3][3] = {{0, n, 0}, {n, c, n}, {0, n, 0}};
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

// 透明区域的棋盘格底纹(8px 两色,深色主题配色)
inline QImage checkerBg(int w, int h) {
    QImage bg(w, h, QImage::Format_RGB32);
    const int cell = 8;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            bg.setPixel(x, y, (((x / cell) + (y / cell)) & 1)
                                  ? 0xFF3A3A40 : 0xFF26262B);
    return bg;
}

} // namespace ImgProc
