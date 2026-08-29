#pragma once
#include <QColor>
#include <QImage>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <functional>

class QPainter;

// ═══════════════════════════════════════════
// 打印排版核心 —— 刻意不依赖 QtWidgets/QPrinter:
//   同一个 printRenderPage() 既喂给真实打印机,也喂给对话框里的预览图,
//   离线夹具还能直接画成 PNG 逐像素看。预览与成品不一致的 bug 就没有藏身处。
// ═══════════════════════════════════════════

// 每页图片数预设(排版只有这几种确切形状,列表里摆 5/7/8 就是骗人)
namespace PrintFit {
// 适应边框(可放大) / 不放大(小图按原始像素居中) / 原始尺寸(按文件自带 DPI) / 填充裁边
enum { Fit = 0, NoUpscale = 1, Actual = 2, Fill = 3 };
}
namespace PrintCaption {
enum { None = 0, Name = 1, NameSize = 2, NameDate = 3 };
}
namespace PrintBg {
enum { White = 0, Black = 1, None = 2 };
}

struct PrintOptions {
    int    perPage      = 1;      // 1/2/3/4/6/9
    int    fit          = PrintFit::Fit;
    bool   landscape    = false;
    double marginMm     = 5.0;    // 页边距(单值四边同)
    double gapMm        = 2.0;    // 图与图间距
    int    caption      = PrintCaption::Name;
    int    captionPt    = 9;      // 说明文字字号(pt)
    bool   grayscale    = false;
    int    background   = PrintBg::White;
    bool   border       = false;  // 每张图 0.3mm 细描边
};

// 一张图的静态信息(标题文案与"原始尺寸"排版都要用,只在这里定一次口径)
struct PrintImageInfo {
    QSize   px;                 // 像素尺寸(读得到图时以图为准,读不到时用这个占位)
    qreal   dpiX = 0;           // 水平 DPI;0=文件没写,按 96 处理
    QString name;
    QString dateText;           // 修改日期(已格式化)
    bool    ok = false;         // false=读不到(文件没了/格式不支持)
};

using PrintInfoFetcher  = std::function<PrintImageInfo(int index)>;
using PrintImageFetcher = std::function<QImage(int index)>;   // 空 QImage=读失败

// 每页张数 → 行列。横向优先左右铺,纵向优先上下铺。
void printGridShape(int perPage, bool landscape, int& cols, int& rows);

// 共几页
int  printPageCount(int imageCount, int perPage);

// 一页画完的实况,给状态栏/进度框用(不静默吞掉坏文件与超尺寸)
struct PrintPageResult {
    int drawn  = 0;   // 成功画出的图片数
    int failed = 0;   // 读不到(文件消失/格式不支持)—— 仍占一格并画叉
    int shrunk = 0;   // "原始尺寸"放不下而被收缩的张数
};

// 渲染第 pageIndex 页(0 基)。paintRect 是"可印区"矩形,单位=绘制坐标设备像素,
// 并且**带 origin 偏移**(打印机的可印区不在纸张左上角);dpi 用于 mm 换算。
// 读到坏文件也占一格并画叉,不静默少画 —— 少了没人会发现。
int  printRenderPage(QPainter& g, const QRectF& paintRect, qreal dpi,
                     const PrintOptions& opt,
                     const QStringList& paths, int pageIndex,
                     const PrintInfoFetcher& info,
                     const PrintImageFetcher& image,
                     PrintPageResult* result = nullptr);
