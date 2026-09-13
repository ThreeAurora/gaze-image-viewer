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

bool PreviewPanel::inFullscreen() const {
    const QWidget* w = window();
    return w && w->isFullScreen();
}

// 同一角色在两处各有一套设置:全屏时改用 Fullscreen/*,否则 Viewer/*
QString PreviewPanel::modeKey(const char* suffix) const {
    return QString::fromLatin1(inFullscreen() ? "Fullscreen/" : "Viewer/")
         + QString::fromLatin1(suffix);
}

QColor PreviewPanel::backdropColor() const {
    // 查看器与浏览器预览窗格用各自的背景色设置(XnView 同)。
    // 未设置时默认底色随主题(深黑浅白);全屏放映厅恒黑不受主题影响
    const QString key = (m_viewerMode || inFullscreen())
        ? modeKey("backColor")
        : QStringLiteral("Browser/previewBackColor");
    const QColor def = (key == QLatin1String("Fullscreen/backColor"))
        ? QColor(QStringLiteral("#000000"))
        : QColor(Theme::T("#000000", "#FFFFFF"));
    QColor c(AppSettings::instance().get(key, def.name()).toString());
    return c.isValid() ? c : def;
}

// 透明像素下的挡板底纹(Viewer/checkerMode):16px 两色方格
static QImage checkerTile(const QColor& base) {
    const int cell = 8;
    QImage img(cell * 2, cell * 2, QImage::Format_ARGB32_Premultiplied);
    img.fill(base);
    QPainter p(&img);
    QColor ink = base.lightness() > 128 ? base.darker(140) : base.lighter(160);
    p.setPen(Qt::NoPen);
    p.setBrush(ink);
    p.drawRect(0, 0, cell, cell);
    p.drawRect(cell, cell, cell, cell);
    p.end();
    return img;
}

void PreviewPanel::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event);
    QPainter p(this);
    p.fillRect(rect(), backdropColor());
    if (pp_impl::s_bool("Viewer/checkerMode", false)) {
        QBrush tile(checkerTile(backdropColor()));
        tile.setStyle(Qt::TexturePattern);
        p.fillRect(rect(), tile);
    }

    // Viewer/selectedOverlay:画面构图辅助线(0 正常=不画 1 三分法 2 黄金分割)
    // 画在图片标签的几何范围内 —— 图片是自由定位的,坐标取它的当前位置
    const int guide = pp_impl::s_int("Viewer/selectedOverlay", 0);
    if (guide > 0 && m_mode == "image" && m_imgLabel->isVisible()) {
        const QRect r = m_imgLabel->geometry();
        if (r.width() > 40 && r.height() > 40) {
            p.setPen(QPen(QColor(255, 255, 255, 110), 1, Qt::DotLine));
            const double f1 = (guide == 1) ? 1.0 / 3.0 : 1.0 - 0.618;
            const double f2 = 1.0 - f1;
            for (double f : {f1, f2}) {
                const int x = r.x() + int(r.width()  * f);
                const int y = r.y() + int(r.height() * f);
                p.drawLine(x, r.y(), x, r.bottom());
                p.drawLine(r.x(), y, r.right(), y);
            }
            if (guide == 2) {   // 黄金分割再补两条对角线方向的螺旋基准线
                p.setPen(QPen(QColor(255, 255, 255, 70), 1, Qt::DotLine));
                p.drawLine(r.topLeft(), r.bottomRight());
                p.drawLine(r.topRight(), r.bottomLeft());
            }
        }
    }
}

// 背景色/挡板/图片边框变更时调用(构造 + AppSettings::changed)
void PreviewPanel::applyBackdrop() {
    update();
    // Viewer/showBorder:图片外框(默认关)
    const bool border = pp_impl::s_bool("Viewer/showBorder", false);
    m_imgLabel->setStyleSheet(border
        ? QStringLiteral("QLabel{border:1px solid #FFFFFF;background:transparent;}")
        : QStringLiteral("QLabel{background:transparent;}"));
}

// ═══════════════════════════════════════════
// 设置活接线:查看器/全屏界面元素
// ═══════════════════════════════════════════
void PreviewPanel::applyViewerChrome() {
    updateOverlayScrollbars();
    updateFloatBar();
    updatePanTool();
    updateSelectionHighlight();
    updateRatingBadge();
}

// Viewer|Fullscreen/showScrollbar:图比视口大时才出现,位置贴边浮在图上
void PreviewPanel::updateOverlayScrollbars() {
    const bool on = m_mode == "image" && m_origPix
                    && pp_impl::s_bool(modeKey("showScrollbar"), false);
    if (!on) {
        m_hScroll->hide();
        m_vScroll->hide();
        return;
    }
    const int iw = m_imgLabel->width(), ih = m_imgLabel->height();
    const int bw = width(), bh = height();
    const bool needH = iw > bw, needV = ih > bh;
    const int thick = 10;
    if (needH) {
        m_hScroll->setGeometry(0, bh - thick, bw - (needV ? thick : 0), thick);
        m_hScroll->setRange(0, iw - bw);
        m_hScroll->setPageStep(bw);
        m_hScroll->setSingleStep(24);
        m_hScroll->setValue(qBound(0, -m_imgLabel->x(), iw - bw));
        m_hScroll->show();
    } else m_hScroll->hide();
    if (needV) {
        m_vScroll->setGeometry(bw - thick, 0, thick, bh - (needH ? thick : 0));
        m_vScroll->setRange(0, ih - bh);
        m_vScroll->setPageStep(bh);
        m_vScroll->setSingleStep(24);
        m_vScroll->setValue(qBound(0, -m_imgLabel->y(), ih - bh));
        m_vScroll->show();
    } else m_vScroll->hide();
}

// Fullscreen/showToolbar(常显) + Fullscreen/floatView(鼠标移到顶侧/右侧才浮现)
// #208:已可见且判定不变时直接早退 —— 该条几何固定(顶中),每帧 adjustSize/
// move/raise 全是白烧的;hide 同理只在真可见时才调
void PreviewPanel::updateFloatBar(const QPoint* cursor) {
    // m_gFullView(G 全屏预览):工具条并入胶片条右端按钮区,这里整个让位(#209);
    // 查看器/F11 全屏不受影响,照旧浮现
    if (!inFullscreen() || m_gFullView) {
        if (m_floatBar->isVisible()) m_floatBar->hide();
        return;
    }
    const bool always = pp_impl::s_bool("Fullscreen/showToolbar", false);
    const bool floating = pp_impl::s_bool("Fullscreen/floatView", true);
    bool show = always;
    if (!show && floating && cursor) {
        const int edge = 48;
        show = cursor->y() <= edge || cursor->x() >= width() - edge;
    }
    if (!show) { if (m_floatBar->isVisible()) m_floatBar->hide(); return; }
    if (m_floatBar->isVisible()) return;
    m_floatBar->adjustSize();
    m_floatBar->move((width() - m_floatBar->width()) / 2, 10);
    m_floatBar->raise();
    m_floatBar->show();
}

// 2026-09-08 用户令:G 全屏视频的进度条(控制栏)默认隐藏,光标挪进底部
// 触发带才浮现,挪走即藏 —— 与顶部胶片条(到顶出)/左右浮动钮(到边出)
// 同一套"全屏只留画面,光标到边才出装饰"的语义。
// 管辖范围:仅 m_gFullView && video。窗口态/查看器/F11 全屏的显隐仍归
// showVideo 的 Fullscreen/showPlaybar 规则;GIF/音频各走各的 chrome。
// Fullscreen/showPlaybar=false 仍是总闸:总闸关着就永不浮现。
// 坐标:面板局部。cursor=nullptr 表示"无光标信息"(进场/切文件)→ 默认藏。
void PreviewPanel::updateGFullPlaybar(const QPoint* cursor) {
    if (m_gFullView && inFullscreen() && m_mode == "video") {
        if (m_isLivePhoto || !pp_impl::s_bool("Fullscreen/showPlaybar", true)) {
            // Live Photo 无控制栏 / 总闸关着:维持 showVideo 的隐藏,不接管
            m_gPlaybarAuto = false;
            return;
        }
        m_gPlaybarAuto = true;
        const int edge = 56;   // 底部触发带高度(与顶部胶片条 kFilmEdge 同档)
        const bool want = cursor && cursor->y() >= height() - edge;
        if (want != m_controlBar->isVisible()) {
            m_controlBar->setVisible(want);
            // 栏预留高度 0↔40 变了,视频信封跟着重铺;只 setVisible 的话
            // 信封停在旧几何上,浮现栏会遮住画面底部一条
            syncVideoChildren();
        }
        return;
    }
    // 非 G 全屏视频:只在自己接管过(G 全屏期间藏过/显过)时把栏还回
    // 普通规则,绝不越权改 showVideo/applyGifChrome 的裁决。
    // GIF 形态的控制栏由 applyGifChrome 无条件 show,同样不归这里管
    if (!m_gPlaybarAuto) return;
    m_gPlaybarAuto = false;
    if (m_isGif) return;
    const bool live = m_isLivePhoto;
    const bool playbar = !inFullscreen() || pp_impl::s_bool("Fullscreen/showPlaybar", true);
    m_controlBar->setVisible(!live && playbar);
}

// Viewer/showRating:查看器右上角显示当前文件的颜色标记圆点
// (键名沿用历史 showRating;程序只有颜色标记,没有评级概念)
void PreviewPanel::updateRatingBadge() {
    const bool on = pp_impl::s_bool("Viewer/showRating", true) && !m_filePath.isEmpty();
    if (!on) { if (m_ratingDot) m_ratingDot->hide(); return; }
    const int color = LabelStore::instance().colorFor(m_filePath);
    if (color <= 0) { if (m_ratingDot) m_ratingDot->hide(); return; }
    if (!m_ratingDot) {
        m_ratingDot = new QLabel(this);
        m_ratingDot->setFixedSize(14, 14);
    }
    m_ratingDot->setStyleSheet(
        QString("QLabel{background:%1;border:1px solid #FFFFFF;border-radius:7px;}")
            .arg(LabelStore::colorValue(color).name()));
    m_ratingDot->move(width() - 24, 12);
    m_ratingDot->raise();
    m_ratingDot->show();
}

// 2026-09-09 用户令:LIVE 徽章不只在播放时出现 —— 静态图识别为 live photo
// 就亮在预览区右上角,提示"这是动态照片,单击可播放动态部分"。挂在面板上,
// 与 RAW/CMYK 钮同机制:同位右上角、随面板缩放重定位、得 raise 盖在最上层。
// showImage/applyImage/resize 都走这里(播放 live 时 m_isLivePhoto=true 保持亮)。
void PreviewPanel::updateLiveBadge() {
    if (!m_liveBadge) return;
    // 亮徽章的条件:当前文件是 live photo(识别期 m_liveInfo 已置位)
    // 或正在播放它的动态视频(finishLivePhoto 回到静态图前 m_isLivePhoto=true)
    const bool isLive = m_liveInfo.has_value() || m_isLivePhoto;
    if (!isLive || (m_mode != "image" && !m_isLivePhoto)) {
        m_liveBadge->hide();
        return;
    }
    m_liveBadge->raise();
    m_liveBadge->adjustSize();
    m_liveBadge->move(width() - m_liveBadge->width() - 12, 12);
    m_liveBadge->show();
}

// 缩略图 pixmap 在导航小窗里的**实际摆放矩形**,坐标系是 m_panTool(蓝框的父)。
// QLabel 用 AlignCenter 画 pixmap,留边时图并不铺满控件;而 m_panThumb 又嵌在
// m_panTool 的 (1,1)。蓝框与指尖映射都只走这一个函数,不在两处各算一遍偏移
// ——分开算时一处是缩略图坐标、一处是工具条坐标,差的就是那 1px(#111)
QRect PreviewPanel::navPixmapRect() const {
    const QPixmap tp = m_panThumb->pixmap();
    if (tp.isNull()) return {};
    const QRect c = m_panThumb->contentsRect();
    return QRect(m_panThumb->pos()
                     + QPoint(c.x() + (c.width()  - tp.width())  / 2,
                              c.y() + (c.height() - tp.height()) / 2),
                 tp.size());
}

// Viewer/panTool:右下角导航小窗(缩略图 + 当前视口框)。仅图溢出视口时出现
void PreviewPanel::updatePanTool() {
    const bool on = pp_impl::s_bool("Viewer/panTool", true) && m_mode == "image" && m_origPix
                    && (m_imgLabel->width() > width() || m_imgLabel->height() > height());
    if (!on) { m_panTool->hide(); return; }

    const int boxW = m_panThumb->width() - 4, boxH = m_panThumb->height() - 4;
    if (m_panKey != m_filePath) {
        m_panThumb->setPixmap(m_origPix->scaled(boxW, boxH, Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation));
        m_panKey = m_filePath;
    }
    // 蓝框=视口在整图中的位置。几何必须与拖动映射(panNavTo)完全一致:
    // 都基于缩略图 pixmap 的实际摆放(KeepAspectRatio 居中,可能留边),
    // 否则拖动时蓝框不落在指尖下。旧实现按整个 box 映射,留边时框会偏
    const QRect pr = navPixmapRect();
    if (!pr.isNull()) {
        const double sx = double(m_imgLabel->width()) / m_origPix->width();
        const double sy = double(m_imgLabel->height()) / m_origPix->height();
        const double vx0 = qMax(0.0, -double(m_imgLabel->x())) / sx;   // 视口左缘的图像 x
        const double vy0 = qMax(0.0, -double(m_imgLabel->y())) / sy;
        const double rw = qMin<double>(1.0, width()  / sx / m_origPix->width())  * pr.width();
        const double rh = qMin<double>(1.0, height() / sy / m_origPix->height()) * pr.height();
        m_panView->setGeometry(pr.x() + int(vx0 / m_origPix->width() * pr.width()),
                               pr.y() + int(vy0 / m_origPix->height() * pr.height()),
                               qMax(4, int(rw)), qMax(4, int(rh)));
    }
    m_panView->show();
    m_panView->raise();
    m_panTool->move(width() - m_panTool->width() - 12, height() - m_panTool->height() - 12);
    m_panTool->raise();
    m_panTool->show();
}

// 导航小窗拖动:指尖下的缩略图点 → 映射回整图坐标 → 让视口中心对准它。
// 按住蓝框(或缩略图任意处)拖动,蓝框始终跟指尖走,可快速甩到图片任意角落
void PreviewPanel::panNavTo(const QPoint& thumbPos) {
    const QRect pr = navPixmapRect();
    if (!m_origPix || pr.isNull() || pr.width() <= 0 || pr.height() <= 0) return;
    // 入参是缩略图(m_panThumb)坐标,而 pr 是 m_panTool 坐标 —— 同一空间才能相减
    const QPoint p = m_panThumb->mapTo(m_panTool, thumbPos);
    const double nx = qBound(0.0, double(p.x() - pr.x()) / pr.width(),  1.0);
    const double ny = qBound(0.0, double(p.y() - pr.y()) / pr.height(), 1.0);
    // label.x + nx*labelW = 视口中线  →  label.x = 中线 - nx*labelW
    const QPoint pos(width() / 2 - int(nx * m_imgLabel->width()),
                     height() / 2 - int(ny * m_imgLabel->height()));
    m_imgLabel->move(clampedLabelPos(pos));
    updateOverlayScrollbars();
    updatePanTool();
}

// Viewer/showBorder:查看器里给当前图片加白色细框(默认关)。
// 2026-08-30 裁决:highlightSelection 蓝框整个删除,预览区不再有选中强调框;
// 文件列表的选中/悬停框归 filegrid 自绘,与本函数无关
// setStyleSheet 会触发样式重算,而本函数在 resize/切文件时都会被调,
// 所以按最终形态缓存,值没变就一个字节都不碰控件
void PreviewPanel::updateSelectionHighlight() {
    const int want = pp_impl::s_bool("Viewer/showBorder", false) ? 2 : 0;
    if (want == m_labelStyleState) return;
    m_labelStyleState = want;
    switch (want) {
    case 2:
        m_imgLabel->setStyleSheet(
            QStringLiteral("QLabel{border:1px solid #FFFFFF;background:transparent;}"));
        break;
    default:
        m_imgLabel->setStyleSheet(QStringLiteral("QLabel{background:transparent;}"));
        break;
    }
}

// #104:切源期间的"挡住上一路画面"。
// 两件事一起做:升起黑色遮罩(第二道防线)+ **把 m_vw 整个藏起来**(真正生效的那一刀)。
// 为什么必须藏:QVideoWidget 内部是 createWindowContainer(QVideoWindow),那是一个
// 原生子窗口,永远压在非原生兄弟控件之上 —— 遮罩盖不到它。实测(cache/tmp/
// vw_screen_probe.cpp,Windows 合成器下 BitBlt 截屏):断输出后视频面还会把上一路
// 的末帧继续呈现约 50~100ms,这段正是用户看到的"闪回上一张";而 hides 掉的
// m_vw 让屏幕上只剩父窗口的 #0A0A0C 深色底,var=0,一帧残影都没有。
void PreviewPanel::raiseVideoCover() {
    ensureVideoWidget();
    m_videoCover->setGeometry(m_videoWidget->rect());
    m_videoCover->raise();
    m_videoCover->show();
    if (m_vw) m_vw->hide();     // ← 真正挡住原生视频窗的那一刀
    m_coverArmed = true;
}

// #104:遮罩的收回权交给"本路源的第一帧"。
// 原先由 playbackStateChanged(PlayingState) 收回 —— 但 PlayingState 比首帧早到,
// 那一刻 QVideoWidget 的表面里还是上一段视频的末帧,于是露出"闪回上一张"。
// 必须在 attach 之后调用(此前 player->videoSink() 不属于 m_vw)。
void PreviewPanel::armCoverUntilFirstFrame() {
    if (!m_player) return;
    QVideoSink* vs = m_player->videoSink();
    if (!vs) {
        // 布不了防就必须立刻放开:否则 m_vw 一直藏着、又没人等首帧 = 永久黑屏。
        // 此时输出已断,视频面本就已经是黑的,放开不会放出残帧。
        Logger::event(QStringLiteral("#104 arm failed: no videoSink -> reveal"));
        revealVideo();
        return;
    }
    disconnect(m_coverConn);
    m_coverArmed = true;
    m_coverConn = connect(vs, &QVideoSink::videoFrameChanged, this,
                          [this](const QVideoFrame& f) {
        if (!f.isValid()) return;
        revealVideo();
    });
}

// #104 唯一的"露出"出口:收遮罩 + 把视频控件放出来。
// 所有兜底路径(未布防时的 PlayingState、InvalidMedia)都走这里,
// 避免"藏起来却没人放出来"变成永久黑屏。
void PreviewPanel::revealVideo() {
    disconnect(m_coverConn);
    m_coverArmed = false;
    if (m_videoCover) m_videoCover->hide();
    if (m_vw) m_vw->show();
}

// Viewer/inZoomFilter / outZoomFilter:索引 0 = "无"(快速最近邻)。
// Qt 只暴露 Fast/Smooth 两档,Bilinear/Bicubic/Spline/Lanczos 统一落到 Smooth
static Qt::TransformationMode zoomFilterMode(const QString& key) {
    return pp_impl::s_int(key, 1) == 0 ? Qt::FastTransformation : Qt::SmoothTransformation;
}

void PreviewPanel::render() {
    if (!m_origPix || m_origPix->isNull()) return;
    // Viewer/pixelRatio:非正方形像素( DV/PAL 等)按像素比拉宽后显示
    const double par = pixelAspect();
    int w = static_cast<int>(m_origPix->width() * m_scale * par);
    int h = static_cast<int>(m_origPix->height() * m_scale);
    if (w < 1) w = 1; if (h < 1) h = 1;
    const Qt::TransformationMode mode = zoomFilterMode(
        m_scale > 1.0 ? QStringLiteral("Viewer/inZoomFilter")
                      : QStringLiteral("Viewer/outZoomFilter"));

    m_imgLabel->setFixedSize(w, h);
    if (m_isGif) {
        // GIF:几何由第一帧(m_origPix)算,画面取当前帧(m_gifPix)。这里不重绘
        // m_origPix,否则每次缩放/改窗都会把动画闪回第一帧
        if (!m_gifPix.isNull()) {
            const QSize want = m_imgLabel->size();
            m_imgLabel->setPixmap(m_gifPix.size() == want
                ? m_gifPix
                : m_gifPix.scaled(want, Qt::IgnoreAspectRatio, Qt::SmoothTransformation));
        }
    } else {
        // Viewer/twoPassRender(默认关):第一遍快速最近邻先把画面撑起来,
        // 下一轮事件循环再用抗锯齿重画一次(大图缩放时先出形再出质)
        const bool twoPass = pp_impl::s_bool("Viewer/twoPassRender", false);
        if (twoPass && !m_secondPass) {
            m_imgLabel->setPixmap(m_origPix->scaled(w, h, Qt::KeepAspectRatio,
                                                    Qt::FastTransformation));
            QMetaObject::invokeMethod(this, [this]() {
                m_secondPass = true;
                render();
                m_secondPass = false;
            }, Qt::QueuedConnection);
        } else {
            const QPixmap& src = processedFor(w, h, *m_origPix);
            m_imgLabel->setPixmap(src.scaled(w, h, Qt::KeepAspectRatio, mode));
        }
    }
    // 居中定位(允许负偏移:图大于视口时四边裁剪,拖动查看各部分)
    m_imgLabel->move((width() - w) / 2, (height() - barReserve() - h) / 2);
    m_lastScale = m_scale;
    updateOverlayScrollbars();
    updatePanTool();
}

// m_videoWidget 的子件(vw/cover)不随布局自动重排,而 videoWidget 自身的矩形
// 在 hide/show 切换间会被布局改写:GIF 形态(imgSpace 顶 stretch)时布局把
// 隐藏的 videoWidget 改写成"半高矩形",切回视频后若不同步,vw 就停在旧矩形上
// ——画面缩在顶部一半、下面露出纯黑底(2026-08-30 用户截图实锤)。
// resizeEvent 与 showVideo(含布局激活后的 0ms 补刀)双入口调用。
void PreviewPanel::syncVideoChildren() {
    if (!m_videoWidget) return;
    for (auto* child : m_videoWidget->children()) {
        if (auto* w = qobject_cast<QWidget*>(child)) {
            if (w == m_liveBadge) continue; // 徽章保持右上角小尺寸，不铺满
            // 视频面按画面宽高比铺(2026-09-06 用户令:浅色主题信封四周应为白):
            // QVideoWidget 是原生 D3D 画布,它自己的信封恒黑不吃调色板 ——
            // 把它收缩到画面比例,四周露出 pvVideo 的主题底色(浅白/深黑)
            if (w == m_vw && m_mode == "video"
                && m_videoSize.width() > 0 && m_videoSize.height() > 0
                && m_videoWidget->width() > 4 && m_videoWidget->height() > 4) {
                // 与图片 fitAuto 同口径:v/音频用同一块"可站地"——扣除控制栏。
                // 此前按 m_videoWidget 全高算包络,栏(40px)占据的高度还在,视频
                // 信封再从中裁比例,成品比同面板的图片矮一大截(2026-09-09 用户
                // 报:"播放的视频比图片的宽高要小,应同样贴合扩展框")。
                // live 播放时控制栏隐藏→barReserve==0,信封即铺满整个预览区。
                // 面板极矮(拖分屏 <40px)时差值可能为负,钳成 1 防负高几何
                const int usableH = qMax(1, m_videoWidget->height() - barReserve());
                const double va = double(m_videoSize.width()) / m_videoSize.height();
                const double ba = double(m_videoWidget->width()) / qMax(1, usableH);
                QRect r(0, 0, m_videoWidget->width(), usableH);
                if (va > ba) {   // 画面更宽:横向顶满,纵向居中
                    const int h = qMax(1, int(r.width() / va));
                    r.setHeight(h);
                    r.moveTop(qMax(0, (usableH - h) / 2));
                } else {         // 画面更高:纵向顶满,横向居中
                    const int wd = qMax(1, int(r.height() * va));
                    r.setWidth(wd);
                    r.moveLeft((m_videoWidget->width() - wd) / 2);
                }
                w->setGeometry(r);
                continue;
            }
            w->setGeometry(m_videoWidget->rect());
        }
    }
}
