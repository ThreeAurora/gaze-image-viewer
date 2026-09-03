// ═══════════════════════════════════════════
// FilmStrip —— #203 全屏胶片条控件(实现),设计约定见 filmstrip.h 头注释
// ═══════════════════════════════════════════

#include "views/filmstrip.h"
#include "thumbnailer.h"
#include "constants.h"
#include "i18n.h"

#include <QLabel>
#include <QFileInfo>
#include <QPainter>
#include <QScrollBar>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QStyledItemDelegate>
#include <QToolButton>
#include <QGridLayout>
#include <QStyle>
#include <QIcon>

namespace {
constexpr int kThumbW  = 72;
constexpr int kThumbH  = 64;
constexpr int kGap     = 4;
constexpr int kCaptionH = 20;
constexpr int kPanThresh = 6;     // 按住位移超过这个像素才算拖
constexpr int kBtnZone   = 60;    // #209:右端按钮区宽(2x2 网格)
// 标准图标染白(条底是深色,原生图标是深色的看不见;与 pp_impl::whiteIcon
// 同一套做法的本地副本 —— 那个头注明只供 previewpanel 各编译单元使用)
QIcon whiteIcon(const QIcon& base) {
    const QPixmap pm = base.pixmap(32, 32);
    QPixmap white(pm.size());
    white.fill(Qt::transparent);
    QPainter p(&white);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(white.rect(), QColor("#FFFFFF"));
    p.end();
    return QIcon(white);
}
}

int FilmStrip::preferredHeight() { return 6 + kThumbH + kCaptionH; }

// ── 数据模型 ──
QVariant FilmStripModel::data(const QModelIndex& idx, int role) const {
    if (!idx.isValid() || idx.row() >= m_paths.size()) return {};
    if (role == Qt::ToolTipRole) return QFileInfo(m_paths.at(idx.row())).fileName();
    if (role == ThumbRole) {
        if (QImage* t = m_thumbs.object(m_paths.at(idx.row()))) return *t;
        return QImage();
    }
    return {};
}

void FilmStripModel::reset(const QStringList& paths) {
    beginResetModel();
    m_paths = paths;
    m_rowOf.clear();
    for (int i = 0; i < paths.size(); ++i) m_rowOf.insert(paths.at(i), i);
    endResetModel();   // 缩略图缓存不清:同目录刷新(增删文件)时已解的接着用
}

void FilmStripModel::setThumb(const QString& path, const QImage& img) {
    const auto it = m_rowOf.constFind(path);
    if (it == m_rowOf.constEnd() || img.isNull()) return;
    m_thumbs.insert(path, new QImage(img), 1);
    const QModelIndex i = index(it.value());
    emit dataChanged(i, i, { ThumbRole });
}

// ── 绘制代理:圆角卡 + 当前蓝框 + 悬停亮描边 ──
namespace {
class FilmStripDelegate : public QStyledItemDelegate {
public:
    explicit FilmStripDelegate(FilmStrip* v) : QStyledItemDelegate(v), m_view(v) {}
    QSize sizeHint(const QStyleOptionViewItem&, const QModelIndex&) const override {
        return { kThumbW, kThumbH };
    }
    void paint(QPainter* p, const QStyleOptionViewItem& opt,
               const QModelIndex& idx) const override {
        p->save();
        p->setRenderHint(QPainter::Antialiasing, true);
        const bool cur = idx.row() == m_view->currentRow();
        const bool hov = idx.row() == m_view->hoverRow();
        p->setPen(cur ? QPen(QColor(0, 120, 215), 2)
                : hov ? QPen(QColor(C_TEXT), 1)
                      : QPen(QColor(C_SEPARATOR), 1));
        p->setBrush(QColor(0x26, 0x26, 0x2B));
        p->drawRoundedRect(opt.rect.adjusted(0, 0, -1, -1), 4, 4);
        const QImage img = idx.data(FilmStripModel::ThumbRole).value<QImage>();
        if (!img.isNull()) {
            const QRect ir = opt.rect.adjusted(3, 3, -3, -3);
            const QImage sc = img.scaled(ir.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
            p->drawImage(ir.x() + (ir.width() - sc.width()) / 2,
                         ir.y() + (ir.height() - sc.height()) / 2, sc);
        }
        p->restore();
    }
private:
    FilmStrip* m_view;
};
}

FilmStrip::FilmStrip(QWidget* parent)
    : QListView(parent), m_model(new FilmStripModel(this)) {
    setObjectName(QStringLiteral("filmStrip"));
    setStyleSheet(QString::fromUtf8(
        "QWidget#filmStrip{background:rgba(18,18,24,235);border:1px solid #3A3A42;"
        "border-radius:8px;}"
        "QWidget#filmStrip::viewport{background:transparent;}"));
    setFrameShape(QFrame::NoFrame);
    setModel(m_model);
    setViewMode(QListView::IconMode);
    setFlow(QListView::LeftToRight);
    setWrapping(false);
    setResizeMode(QListView::Adjust);
    setMovement(QListView::Static);
    setUniformItemSizes(true);
    setSpacing(kGap);
    setSelectionMode(QAbstractItemView::NoSelection);   // 选中态由代理按 currentRow 画
    setFocusPolicy(Qt::NoFocus);   // 焦点必须留在预览面板:方向键切图不能被条吃掉
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setMouseTracking(true);
    viewport()->setMouseTracking(true);
    setViewportMargins(8, 6, kBtnZone + 10, kCaptionH);   // 底部行留给题注,右端留给按钮区
    setItemDelegate(new FilmStripDelegate(this));
    setFixedHeight(preferredHeight());

    // ── 右端按钮区(#209):G 全屏顶中的浮动工具条并入条里 ──
    m_btnBar = new QWidget(this);
    m_btnBar->setStyleSheet(QString::fromUtf8(
        "QToolButton{background:transparent;border:none;border-radius:4px;padding:0;}"
        "QToolButton:hover{background:#3A3A42;}"));
    auto* grid = new QGridLayout(m_btnBar);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(0);
    auto mkBtn = [&](QStyle::StandardPixmap sp, const QString& tip, int r, int c,
                     auto&& fn) {
        auto* b = new QToolButton(m_btnBar);
        b->setIcon(whiteIcon(style()->standardIcon(sp)));
        b->setIconSize(QSize(14, 14));
        b->setFixedSize(30, 32);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        grid->addWidget(b, r, c);
    };
    mkBtn(QStyle::SP_MediaSkipBackward, gazeTr("上一个文件"), 0, 0,
          [this] { emit navRelative(-1); });
    mkBtn(QStyle::SP_MediaSkipForward,  gazeTr("下一个文件"), 0, 1,
          [this] { emit navRelative(1); });
    mkBtn(QStyle::SP_DialogResetButton, gazeTr("适应窗口"),   1, 0,
          [this] { emit fitRequested(); });
    mkBtn(QStyle::SP_DialogCloseButton, gazeTr("退出全屏"),   1, 1,
          [this] { emit exitRequested(); });

    m_caption = new QLabel(this);
    m_caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    m_caption->setStyleSheet(QString::fromUtf8(
        "QLabel{background:transparent;color:#8A8A94;font-size:11px;}"));
    m_caption->hide();

    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, [this](int) {
        requestVisibleThumbs();
    });
    connect(&Thumbnailer::instance(), &Thumbnailer::thumbnailReady, this,
            [this](const QString& path, const QImage& img) {
        m_model->setThumb(path, img);   // 不在表里的路径 setThumb 自行丢弃
    });
}

void FilmStrip::setEntries(const QStringList& paths, const QString& current) {
    m_requested.clear();
    m_pressRow = -1;
    m_panning  = false;
    m_hoverRow = -1;
    m_model->reset(paths);
    horizontalScrollBar()->setValue(0);
    int row = -1;
    if (!current.isEmpty()) {
        for (int i = 0; i < paths.size(); ++i)
            if (paths.at(i) == current) { row = i; break; }
    }
    applyCurrent(row, true);
    requestVisibleThumbs();
}

void FilmStrip::syncCurrent(const QString& path) {
    if (path.isEmpty() || currentPath() == path) return;
    for (int i = 0; i < m_model->rowCount(); ++i)
        if (m_model->pathAt(i) == path) { applyCurrent(i, true); return; }
}

void FilmStrip::applyCurrent(int row, bool center) {
    if (m_currentRow != row) {
        m_currentRow = row;
        viewport()->update();   // 蓝框从旧位置挪到新位置
        updateCaption();
    }
    if (row >= 0 && center)
        scrollTo(m_model->index(row), QAbstractItemView::PositionAtCenter);
}

void FilmStrip::updateCaption() {
    const int n = m_model->rowCount();
    if (m_currentRow < 0 || n <= 0) { m_caption->hide(); return; }
    m_caption->setText(gazeTr("%1 · %2 / %3")
        .arg(QFileInfo(m_model->pathAt(m_currentRow)).fileName())
        .arg(m_currentRow + 1).arg(n));
    m_caption->show();
}

// 懒取缩略图:可见区 ±1 张,按均匀条目宽从滚动值直接算行号(ScrollPerPixel 下精确,
// 不必 indexAt 往回猜)。已入队/已缓存的不重复入队。
void FilmStrip::requestVisibleThumbs() {
    const int n = m_model->rowCount();
    if (n <= 0) return;
    const int rowW = kThumbW + kGap;
    int first = horizontalScrollBar()->value() / rowW;
    first = qBound(0, first, n - 1);
    const int last = qMin(n - 1, first + viewport()->width() / rowW + 1);
    for (int r = first; r <= last; ++r) {
        const QString p = m_model->pathAt(r);
        if (p.isEmpty() || m_requested.contains(p) || m_model->hasThumb(p)) continue;
        m_requested.insert(p);
        QString su = QFileInfo(p).suffix().toLower();
        if (!su.isEmpty()) su.prepend(QLatin1Char('.'));
        Thumbnailer::instance().enqueue(p, kThumbW, VIDEO_EXTS.count(su) > 0);
    }
}

void FilmStrip::wheelEvent(QWheelEvent* e) {
    QScrollBar* h = horizontalScrollBar();
    if (!e->pixelDelta().isNull()) {          // 触控板:平滑跟手
        const QPoint d = e->pixelDelta();
        h->setValue(h->value() - (d.x() != 0 ? d.x() : d.y()));
    } else {
        const int dy = e->angleDelta().y();
        const int notches = dy / 120;
        if (notches != 0)                     // 滚轮:一格滚两张
            h->setValue(h->value() - notches * (kThumbW + kGap) * 2);
        else                                  // 高分辨率滚轮
            h->setValue(h->value() - dy / 4);
    }
    e->accept();
}

void FilmStrip::mousePressEvent(QMouseEvent* e) {
    // 完全自管,不进 QAbstractItemView 的点击/选中管线
    if (e->button() == Qt::LeftButton) {
        m_pressPos = e->position().toPoint();
        m_pressRow = indexAt(m_pressPos).row();
        m_panning  = false;
    }
    e->accept();   // 右键/中键在条上也不做任何事、不冒泡
}

void FilmStrip::mouseMoveEvent(QMouseEvent* e) {
    const int hr = indexAt(e->position().toPoint()).row();
    if (hr != m_hoverRow) { m_hoverRow = hr; viewport()->update(); }
    if (e->buttons() & Qt::LeftButton) {
        const QPoint d = e->position().toPoint() - m_pressPos;
        if (!m_panning && qAbs(d.x()) + qAbs(d.y()) > kPanThresh) {
            m_panning = true;
            setCursor(Qt::ClosedHandCursor);
        }
        if (m_panning) {
            horizontalScrollBar()->setValue(horizontalScrollBar()->value() - d.x());
            m_pressPos = e->position().toPoint();   // 增量式:每拍从头算
        }
    }
    e->accept();
}

void FilmStrip::mouseReleaseEvent(QMouseEvent* e) {
    // 拖过/按空处/按住挪出再松回别的行 → 都不算点击
    if (e->button() == Qt::LeftButton && !m_panning && m_pressRow >= 0
        && m_pressRow == indexAt(e->position().toPoint()).row()) {
        applyCurrent(m_pressRow, true);
        emit jumpRequested(m_model->pathAt(m_pressRow));
    }
    m_pressRow = -1;
    m_panning  = false;
    setCursor(Qt::ArrowCursor);
    e->accept();
}

void FilmStrip::resizeEvent(QResizeEvent* e) {
    QListView::resizeEvent(e);
    m_caption->setGeometry(8, height() - kCaptionH + 3, width() - 16, 14);
    m_btnBar->setGeometry(width() - kBtnZone - 6, 6, kBtnZone, kThumbH);
    requestVisibleThumbs();
}
