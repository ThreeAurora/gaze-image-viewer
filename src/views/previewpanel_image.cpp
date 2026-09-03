#include "previewpanel.h"
#include "livephoto.h"
#include "thumbnailer.h"
#include "logger.h"
#include "wicdecode.h"
#include "settings.h"
#include "labelstore.h"
#include "markdown.h"
#include "pdfrender.h"
#include "textlimit.h"
#include "imgproc.h"
#include "constants.h"
#include "viewerhotkeys.h"
#include "shelldelete.h"
#include "fileentry.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QtMath>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QSplitter>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QMimeData>
#include <QMediaDevices>
#include <QWidgetAction>
#include <QThreadPool>
#include <QTextEdit>
#include <QFile>
#include <QMetaObject>
#include <QClipboard>
#include <QBrush>
#include <QScrollBar>
#include <cmath>
#include <QStyle>
#include <QWidgetAction>
#include <QMenu>
#include <QVideoFrame>
#include <QVideoSink>
#include "previewpanel_internal.h"

// Viewer/pixelRatio:非正方形像素的显示宽高比
double PreviewPanel::pixelAspect() const {
    static const double ratios[] = {1.00, 0.91, 0.95, 1.09, 1.20,
                                    1.33, 1.46, 1.50, 1.90, 2.00};
    const int i = qBound(0, pp_impl::s_int("Viewer/pixelRatio", 0), 9);
    double par = ratios[i];
    // General/dpiAdjust(#122):X/Y DPI 不等时横向按各自的 DPI 换算。
    //   纵向定标在 oneToOneScale(用 Y DPI),这里再乘 dpiY/dpiX 修横轴 ——
    //   走的正是 Viewer/pixelRatio 这条现成的"非正方形像素"通道,不另起一套数学。
    //   只在上一项 General/exifDpi 勾上时参与:它换算的是物理尺寸,不是像素数。
    if (pp_impl::s_bool("General/exifDpi", false) && pp_impl::s_bool("General/dpiAdjust", false)
        && m_dpiX >= 24.0 && m_dpiY >= 24.0 && qAbs(m_dpiX - m_dpiY) > 0.5)
        par *= m_dpiY / m_dpiX;
    return par;
}

// Viewer/zoomMode = 0(固定):缩放在预设档位之间跳,不再连续无级变化
double PreviewPanel::stepZoom(double cur, bool up) const {
    static const double steps[] = {0.05, 0.10, 0.16, 0.25, 0.33, 0.50, 0.66, 0.75,
                                   1.00, 1.50, 2.00, 3.00, 4.00, 6.00, 8.00, 10.00};
    constexpr int n = int(sizeof(steps) / sizeof(steps[0]));
    if (up) {
        for (int i = 0; i < n; ++i)
            if (steps[i] > cur + 1e-6) return steps[i];
        return steps[n - 1];
    }
    for (int i = n - 1; i >= 0; --i)
        if (steps[i] < cur - 1e-6) return steps[i];
    return steps[0];
}

// "1:1"只有一个口径:1 图像像素 = 1 屏幕像素。长按看原图、autoFit=1、
// 右键"1:1 像素"与全屏工具条共用这一个函数,不留第二套数学。
// #95 的结论仍然作数:这条口径**默认不看**文件自带的 DPI —— 乘上 dpi/96 之后
//   "1:1"就不再是分辨率意义上的原图,而是"按标称物理尺寸显示",低 DPI 截图会被
//   画得比"适应窗口"还小(用户报的"长按后直接小到看不清")。
// #122 补的是"想要物理尺寸口径"那条路:General/exifDpi 勾上才换算(默认关,
//   与 ini 里无该键时的行为逐字一致),它只可能来自用户在设置里的一次明确勾选。
double PreviewPanel::oneToOneScale() const {
    // General/exifDpi:按标称物理尺寸 → 屏幕 DPI / 图像 Y DPI(图像没写 DPI 就不换算)
    if (pp_impl::s_bool("General/exifDpi", false) && m_dpiY >= 24.0 && m_dpiY <= 4000.0) {
        const QScreen* sc = screen();
        const double sd = sc ? sc->logicalDotsPerInch() : 96.0;
        if (sd > 1.0) return sd / m_dpiY;
    }
    // Viewer/hidpiPixel:1 图像像素映射到 1 物理像素(HiDPI 屏下更锐利,仍不穿透 1:1)
    if (pp_impl::s_bool("Viewer/hidpiPixel", false)) {
        const double dpr = devicePixelRatioF();
        return dpr > 0.01 ? 1.0 / dpr : 1.0;
    }
    return 1.0;
}

// Viewer/autoFit 取值语义:
//   0 上次使用过的   1 不缩放(1:1)      2 适应窗口(默认)
//   3 仅放大小图     4 仅缩小大图        5 适应宽度
//   6 适应高度       7 适应宽或高(取大)  8 适应桌面
//   9 窗口适应图像   → 按"适应窗口"处理(需要改变窗口尺寸,查看器布局尚未支持)
double PreviewPanel::fitScaleFor(const QSize& viewSize) const {
    const double sw = double(viewSize.width())  / m_origPix->width();
    const double sh = double(viewSize.height()) / m_origPix->height();
    const double fit = std::min(sw, sh);
    switch (pp_impl::s_int(modeKey("autoFit"), 2)) {
    // Viewer/resetAutoOnNav:切文件时丢掉"上次使用过的"缩放,重新按自动模式算
    case 0: {
        const bool reset = pp_impl::s_bool("Viewer/resetAutoOnNav", false) && m_navigating;
        return (!reset && m_lastScale > 0) ? m_lastScale : fit;
    }
    // Viewer/hidpiPixel:开=1 图像像素映射到 1 物理像素(HiDPI 下画面变小但最锐利)
    case 1: return oneToOneScale();
    case 3: return std::max(1.0, fit);   // 小图放大到适应,大图保持 1:1
    case 4: return std::min(1.0, fit);   // 大图缩小到适应,小图保持 1:1
    case 5: return sw;
    case 6: return sh;
    case 7: return std::max(sw, sh);
    case 8: {
        const QRect av = QApplication::primaryScreen()->availableGeometry();
        return std::min(double(av.width())  / m_origPix->width(),
                        double(av.height()) / m_origPix->height());
    }
    default: return fit;
    }
}

// 拖拽平移约束(临时 1:1 放大与 Ctrl 缩放态通用):
//   图片某轴 ≤ 预览框 → 该轴锁死居中(两侧黑边等宽,不能挪动)
//   图片某轴 > 预览框 → 允许平移,但图片边缘不进入框内(平到顶即停,不露白边)
QPoint PreviewPanel::clampedLabelPos(QPoint p) const {
    const int imgW = m_imgLabel->width();
    const int imgH = m_imgLabel->height();
    const int boxW = width();
    const int boxH = height() - barReserve();   // 栏占的那一条不算画面可站的地方
    if (imgW <= boxW)
        p.setX((boxW - imgW) / 2);
    else
        p.setX(qBound(boxW - imgW, p.x(), 0));
    if (imgH <= boxH)
        p.setY((boxH - imgH) / 2);
    else
        p.setY(qBound(boxH - imgH, p.y(), 0));
    return p;
}

// 锚点缩放:anchor 下的那个图像点,缩放前后停在 anchor 上不动。
// 尺寸一律取 m_imgLabel 的**当前**与**新**几何,不用 origW*scale 二次推导 ——
// render() 还会乘 Viewer/pixelRatio 并重新居中,自己推的那份跟屏幕上的不是一张图,
// 长按放大的落点就会跑偏(#101);滚轮同理要以光标为中心(#100)。
void PreviewPanel::zoomAnchored(double newScale, const QPoint& anchor) {
    m_scale = newScale;
    if (m_mode != "image" || !m_origPix || m_origPix->isNull()) return;
    // 缩放前:anchor 处在图像上的相对位置(0..1 之外也允许 —— 光标落在黑边上时
    // 相当于盯住画面外的一个虚拟点,缩放后仍按同一比例对齐,不会突然跳开)
    const QSize before = m_imgLabel->size();
    const double fx = before.width()  > 0 ? double(anchor.x() - m_imgLabel->x()) / before.width()  : 0.5;
    const double fy = before.height() > 0 ? double(anchor.y() - m_imgLabel->y()) / before.height() : 0.5;
    render();                        // 先出图(render 会重新居中),再把锚点挪回去
    const QSize after = m_imgLabel->size();
    m_imgLabel->move(clampedLabelPos(QPoint(int(anchor.x() - fx * after.width()),
                                           int(anchor.y() - fy * after.height()))));
    m_dragLabelPos = m_imgLabel->pos();   // 拖动基线跟着走,否则下一次拖动图片跳位
    updateOverlayScrollbars();
    updatePanTool();
}

void PreviewPanel::showImage(const QString& path) {
    m_mode = "image";
    if (m_player) m_player->stop();   // 切到静态图:停媒体(播放器实例保留复用)
    m_placeholder->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_textEdit->hide();
    m_controlBar->hide();
    m_imgSpace->hide();
    if (m_liveBadge) m_liveBadge->hide();

    // GIF 动画:几何与静态图同源(第一帧定尺寸,动画帧只换像素)
    if (path.toLower().endsWith(".gif")) {
        showGif(path);
        return;
    }

    // Viewer/cacheBehind(默认开)= 已定版的"保持上一张画面直到新图就绪";
    // 关掉则立即清空,解码期间露出背景色
    if (!pp_impl::s_bool("Viewer/cacheBehind", true)) {
        delete m_origPix;
        m_origPix = nullptr;
        m_imgLabel->clear();
    }

    // ── 后台解码(用户定版交互模型) ──
    // 切换操作立即返回:网格/选中/状态栏先动,不卡 UI;预览区保持上一张画面
    // (首张前为黑屏),新图后台解码就绪后一次性全清晰替换(无低清过渡态)。
    // 期间旧图仍可交互(拖动/缩放),属预期行为。
    // 相邻预读命中 → 直接应用,零等待
    if (m_preloadCache.contains(path)) {
        applyImage(m_preloadCache.take(path));
        return;
    }
    quint64 gen = ++m_imgReqGen;
    if (m_fullBusy) return;                 // 在飞任务完成后会自动补发最新请求
    decodeFullAsync(path, gen);
}

// 没有可显示位图时的兜底:必须把 label 收到"提示条"大小。
// 沿用上一张图的 setFixedSize,会出现一行小字套在上一张大图的蓝色选中框里(#103)
void PreviewPanel::showImageHint(const QString& text) {
    stopMovie();
    delete m_origPix;
    m_origPix = nullptr;
    m_panKey.clear();
    m_imgLabel->setText(text);
    m_imgLabel->setFixedSize(qBound(140, width() / 3, 300), 40);
    m_imgLabel->move((width() - m_imgLabel->width()) / 2,
                     (height() - m_imgLabel->height()) / 2);
    m_imgLabel->show();
    updatePanTool();          // 没有整图了 → 导航小窗跟着藏
}


// 全尺寸解码统一入口已收口到 ImgProc::decodeFull(imgproc.h)——
// 打印与查看器必须共用同一份解码口径,否则"屏幕上看到的"和"纸上的"会分叉。

void PreviewPanel::decodeFullAsync(const QString& path, quint64 gen) {
    m_fullBusy = true;
    m_issuedGen = gen;
    const bool exifRotate = pp_impl::s_bool("General/exifRotate", true);   // GUI 线程取值
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, path, gen, exifRotate]() {
        QImage img = ImgProc::decodeFull(path, exifRotate);
        auto holder = std::make_shared<QImage>(std::move(img));
        // 队列投递回 GUI 线程;若面板已析构,事件自动丢弃,shared_ptr 兜底释放内存
        QMetaObject::invokeMethod(self, [self, holder, path, gen]() {
            if (!self) return;
            self->onFullDecoded(holder, path, gen);
        }, Qt::QueuedConnection);
    });
}

void PreviewPanel::applyImage(const QImage& img) {
    delete m_origPix;               // 旧画面显示至此(等待期保持),此刻替换
    m_origPix = new QPixmap(QPixmap::fromImage(img));
    // #122:文件自带 DPI。QImage 的 dotsPerMeter 是 EXIF/JPEG APP14/PNG pHYs
    // 的统一落点;没写就是 0,下面的两个开关都不会因此改行为(0 一律不换算)。
    m_dpiX = img.dotsPerMeterX() * 0.0254;
    m_dpiY = img.dotsPerMeterY() * 0.0254;
    m_scale = 1.0;
    m_tempZoom = false;
    m_ctrlZoomed = false;
    m_procKey.clear();              // 换图:gamma/sharpen 后处理缓存作废
    m_panKey.clear();               // 导航小窗缩略图作废
    m_imgLabel->show();
    fitAuto();
    m_navigating = false;           // 缩放已定型,后续 fitAuto 属"重排"而非"切文件"
    applyViewerChrome();
}

void PreviewPanel::onFullDecoded(std::shared_ptr<QImage> img, const QString& path, quint64 gen) {
    m_fullBusy = false;

    if (gen == m_imgReqGen && path == m_filePath && m_mode == "image") {
        if (img && !img->isNull()) {
            applyImage(*img);
        } else {
            showImageHint(gazeTr("无法加载图片"));
        }
    }

    // 解码期间又切过文件 → 自动补发最新期望的任务。
    // 仅当最新期望仍是"待后台解码的静态图"时补发(切到 GIF/视频/音频则不作数)
    if (m_issuedGen != m_imgReqGen && m_mode == "image"
        && !m_filePath.toLower().endsWith(".gif"))
        decodeFullAsync(m_filePath, m_imgReqGen);
    else
        preloadNext();              // 空闲了 → 预读相邻文件(方向键切换零等待)
}

// ── 相邻预读:当前文件解码空闲时,后台预解码下一张/上一张(上限缓存 1 张) ──
void PreviewPanel::preload(const QString& prev, const QString& next) {
    m_preloadQueue.clear();
    if (!pp_impl::s_bool("Viewer/readAhead", true)) return;   // 关预读 = 不做任何后台解码
    if (!next.isEmpty() && !m_preloadCache.contains(next)) m_preloadQueue << next;
    if (!prev.isEmpty() && !m_preloadCache.contains(prev)) m_preloadQueue << prev;
    preloadNext();
}

void PreviewPanel::preloadNext() {
    if (m_fullBusy || m_preloadBusy || m_preloadQueue.isEmpty()) return;
    const QString path = m_preloadQueue.takeFirst();
    if (path.isEmpty() || path == m_filePath || m_preloadCache.contains(path)) {
        preloadNext();
        return;
    }
    m_preloadBusy = true;
    const bool exifRotate = pp_impl::s_bool("General/exifRotate", true);   // GUI 线程取值
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, path, exifRotate]() {
        QImage img = ImgProc::decodeFull(path, exifRotate);
        auto holder = std::make_shared<QImage>(std::move(img));
        QMetaObject::invokeMethod(self, [self, holder, path]() {
            if (!self) return;   // panel destroyed while decoding
            self->m_preloadBusy = false;
            if (holder && !holder->isNull()) {
                self->m_preloadCache.insert(path, *holder);
                self->m_preloadOrder.removeAll(path);
                self->m_preloadOrder.append(path);
                // 上限 2 张(上一张+下一张):12MP≈48MB/张、50MP≈200MB/张,
                // 更多预读只堆内存不提命中(方向突变即废)
                while (self->m_preloadOrder.size() > 2) {
                    self->m_preloadCache.remove(self->m_preloadOrder.takeFirst());
                }
            }
            self->preloadNext();
        }, Qt::QueuedConnection);
    });
}

// 控制栏可见时画面要让出的高度(#94.1):栏是布局里的固定项,而 label 自由
// 定位、不受布局约束 —— 不主动扣,40px 的栏就压在画面下沿,"进度条贴底"
// 看起来像"画面被切了一刀"。栏隐藏(纯静态图)时为 0,不影响原口径。
int PreviewPanel::barReserve() const {
    return (m_controlBar && m_controlBar->isVisible()) ? m_controlBar->height() : 0;
}

void PreviewPanel::fitAuto() {
    if (!m_origPix || m_origPix->isNull()) return;
    m_ctrlZoomed = false;   // 适应窗口 = 回到未 Ctrl 缩放态
    // 以面板尺寸(而非 label 尺寸)计算适应缩放;label 已脱离布局自由定位
    QSize viewSize = size();
    viewSize.rheight() -= barReserve();
    if (viewSize.isEmpty()) return;

    m_scale = fitScaleFor(viewSize);
    if (m_scale < 0.01) m_scale = 0.01;
    if (m_scale > 10.0) m_scale = 10.0;
    render();
}

// Viewer/gamma:线性光重采样(与缩略图 Thumbs/gamma 同一套语义)——
// 直接在 gamma 空间做插值会让放大后的高光/暗部边界失真
const QPixmap& PreviewPanel::processedFor(int w, int h, const QPixmap& src) {
    const bool gammaOn   = pp_impl::s_bool("Viewer/gamma", false);
    const bool sharpenOn = pp_impl::s_bool("Viewer/sharpen", false);
    // key 必须涵盖所有影响输出的开关:仅 WxH 会让"切换 gamma/sharpen"
    // 后仍命中旧缓存,画面卡在上一处理版本。
    const QString key = QString::number(w) + "x" + QString::number(h)
                      + (gammaOn ? "g" : "") + (sharpenOn ? "s" : "");
    const bool need = gammaOn || sharpenOn;
    if (!need) { m_procKey.clear(); return src; }
    if (key == m_procKey && !m_procPix.isNull()) return m_procPix;

    QSize dst = src.size().scaled(w, h, Qt::KeepAspectRatio);
    if (dst.isEmpty()) { m_procKey.clear(); return src; }
    QImage base = src.toImage();
    if (gammaOn)
        base = ImgProc::linearResample(base, dst);
    else
        base = base.scaled(dst, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (sharpenOn) base = ImgProc::sharpen(base, 0.5);
    m_procPix = QPixmap::fromImage(base);
    m_procKey = key;
    return m_procPix;
}
