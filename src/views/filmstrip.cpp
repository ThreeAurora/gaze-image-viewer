// ═══════════════════════════════════════════
// FilmStrip —— #203 全屏胶片条控件(实现),设计约定见 filmstrip.h 头注释
// ═══════════════════════════════════════════

#include "views/filmstrip.h"
#include "thumbnailer.h"
#include "constants.h"
#include "settings.h"
#include "fileentry.h"   // #230:题注 formatSize
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
constexpr int kThumbW  = 96;
constexpr int kThumbH  = 96;      // 加高回撤:72 → 96,条整体随之高一截
constexpr int kGap     = 4;
constexpr int kCaptionH = 30;     // 题注行随条加高放宽一档
constexpr int kPanThresh = 6;     // 按住位移超过这个像素才算拖
constexpr int kStepPx    = 76;    // 滚动联动:像素累积满一格(缩略+间距)切一张图
constexpr int kBtnZone   = 176;   // 右端按钮区宽:2×2 类别方阵(62×2) + 退出竖条(30)
// #230:非当前项图像画在格子的这个占比,当前项吃满格子 —— 不改格子尺寸,
// centerRow/滚动数学零变动,视觉上"当前项比其余大"
constexpr double kIdleShrink = 0.84;
// 类别判定(#225):图(含 RAW)/视频/音频/其他。RAW 独立于 IMAGE_EXTS
// (它不进缩略图管线),但归"图片"按钮管 —— 用户眼里它就是图。
enum Cat { CatImage, CatVideo, CatAudio, CatOther };
Cat catOf(const QString& path) {
    QString su = QFileInfo(path).suffix().toLower();
    if (!su.isEmpty()) su.prepend(QLatin1Char('.'));
    if (IMAGE_EXTS.count(su) || RAW_EXTS.count(su)) return CatImage;
    if (VIDEO_EXTS.count(su)) return CatVideo;
    if (AUDIO_EXTS.count(su)) return CatAudio;
    return CatOther;
}
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

int FilmStrip::preferredHeight() { return 10 + kThumbH + kCaptionH; }

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
        // #230:当前项蓝框加粗到 3px;其余描边照旧
        p->setPen(cur ? QPen(QColor(0, 120, 215), 3)
                : hov ? QPen(QColor(C_TEXT), 1)
                      : QPen(QColor(C_SEPARATOR), 1));
        p->setBrush(QColor(0x26, 0x26, 0x2B));
        p->drawRoundedRect(opt.rect.adjusted(0, 0, -1, -1), 4, 4);
        const QImage img = idx.data(FilmStripModel::ThumbRole).value<QImage>();
        if (!img.isNull()) {
            // #230 当前项放大醒目:当前项图像吃满格子(边距 1px),非当前项
            // 缩到 kIdleShrink 居中 —— "放大"不靠改格子尺寸,滚动/居中数学零变动
            const int inset = cur ? 1 : 3;
            QRect ir = opt.rect.adjusted(inset, inset, -inset, -inset);
            if (!cur) {
                const int dw = int(ir.width() * kIdleShrink);
                const int dh = int(ir.height() * kIdleShrink);
                ir.adjust((ir.width() - dw) / 2, (ir.height() - dh) / 2,
                          -(ir.width() - dw) / 2, -(ir.height() - dh) / 2);
            }
            const QImage sc = img.scaled(ir.size(), Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation);
            p->drawImage(ir.x() + (ir.width() - sc.width()) / 2,
                         ir.y() + (ir.height() - sc.height()) / 2, sc);
        } else {
            // #225:出不了缩略图(音频/文本/可执行/RAW…)把文件名填进格子:
            // 逐字符折行最多 3 行,末行放不下省略,块在格内垂直居中
            QFont f = opt.font;
            f.setPixelSize(11);
            p->setFont(f);
            p->setPen(QColor("#C9C9D2"));
            const QFontMetrics fm(p->fontMetrics());
            const QRect tr = opt.rect.adjusted(5, 5, -5, -5);
            // 模型 ToolTipRole 本来就是文件名(见 FilmStripModel::data),直接拿来用
            QString rest = idx.data(Qt::ToolTipRole).toString();
            QStringList lines;
            const int maxLines = 3;
            for (int li = 0; li < maxLines && !rest.isEmpty(); ++li) {
                if (li == maxLines - 1) {
                    lines << fm.elidedText(rest, Qt::ElideRight, tr.width());
                    break;
                }
                int n = 0;
                qint64 acc = 0;
                while (n < rest.size()
                       && (acc += fm.horizontalAdvance(rest.at(n))) <= tr.width())
                    ++n;
                if (n <= 0) n = 1;   // 一个字都放不下也截一个,防死循环
                lines << rest.left(n);
                rest = rest.mid(n);
            }
            const int lh = fm.height();
            int y = tr.y() + (tr.height() - lines.size() * lh) / 2;
            for (const QString& ln : lines) {
                p->drawText(QRect(tr.x(), y, tr.width(), lh),
                            Qt::AlignLeft | Qt::AlignVCenter, ln);
                y += lh;
            }
        }
        p->restore();
    }
private:
    FilmStrip* m_view;
};
}

FilmStrip::FilmStrip(QWidget* parent)
    : QListView(parent), m_model(new FilmStripModel(this)) {
    setObjectName(QStringLiteral("filmStrip"));   // 样式在应用级 QSS(#89 收敛)
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

    // ── 右端按钮区(#209 并入条;#220 只留退出;#226 用户令:加 图片/视频/音频
    // 三个勾选钮,勾哪类多显示哪类、ini 持久化;退出全屏照旧最右)──
    // #230:三钮与退出钮统一观感 —— 同排等高 32px、勾选态淡蓝不再整块糊底
    m_btnBar = new QWidget(this);
    m_btnBar->setObjectName("filmBtnBar");   // 按钮样式在应用级 QSS(#89 收敛)
    // 四类别钮 2×2 方阵(扁平简洁),退出键独立在最右
    auto* grid = new QGridLayout(m_btnBar);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setSpacing(2);
    AppSettings& st = AppSettings::instance();
    m_showImg = st.get("FilmStrip/showImages", true).toBool();
    m_showVid = st.get("FilmStrip/showVideos", true).toBool();
    m_showAud = st.get("FilmStrip/showAudios", true).toBool();
    m_showOth = st.get("FilmStrip/showOthers", false).toBool();   // 默认不开(#228)
    auto mkCat = [&](const QString& label, const QString& tip,
                     const char* key, bool* flag, int col) {
        auto* b = new QToolButton(m_btnBar);
        b->setText(label);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setChecked(*flag);
        b->setFixedSize(62, 30);
        grid->addWidget(b, col / 2, col % 2);   // 2×2 方阵:0图片 1视频 / 2音频 3其他
        b->setFocusPolicy(Qt::NoFocus);   // 不吃焦点:方向键继续归全屏键位
        connect(b, &QToolButton::toggled, this, [this, key, flag](bool on) {
            *flag = on;
            AppSettings::instance().set(QLatin1String(key), on);
            refilter();   // 蓝框/居中/题注由 locateCurrent 对账;当前文件被滤掉就收框
        });
    };
    mkCat(gazeTr("图片"), gazeTr("显示/隐藏图片"), "FilmStrip/showImages", &m_showImg, 0);
    mkCat(gazeTr("视频"), gazeTr("显示/隐藏视频"), "FilmStrip/showVideos", &m_showVid, 1);
    mkCat(gazeTr("音频"), gazeTr("显示/隐藏音频"), "FilmStrip/showAudios", &m_showAud, 2);
    // #228(2026-09-05 用户令):补第四类"其他"(文本/文档/可执行等),默认不开,
    // 同为记忆式(勾选落 ini)。此前"其他"类没有按钮管、恒显示,现在一并归队。
    mkCat(gazeTr("其他"), gazeTr("显示/隐藏其他类型"), "FilmStrip/showOthers", &m_showOth, 3);
    auto mkBtn = [&](QStyle::StandardPixmap sp, const QString& tip, int c,
                     auto&& fn) {
        auto* b = new QToolButton(m_btnBar);
        b->setObjectName(QStringLiteral("filmClose"));
        b->setIcon(whiteIcon(style()->standardIcon(sp)));
        b->setIconSize(QSize(14, 14));
        b->setFixedSize(30, 32);
        b->setToolTip(tip);
        connect(b, &QToolButton::clicked, this, fn);
        grid->addWidget(b, 0, c);
    };
    auto* exitBtn = new QToolButton(m_btnBar);
    exitBtn->setObjectName(QStringLiteral("filmClose"));
    exitBtn->setIcon(whiteIcon(style()->standardIcon(QStyle::SP_DialogCloseButton)));
    exitBtn->setIconSize(QSize(14, 14));
    exitBtn->setFixedSize(30, 62);   // 竖条,与 2×2 方阵同高
    exitBtn->setToolTip(gazeTr("退出全屏"));
    connect(exitBtn, &QToolButton::clicked, this, [this] { emit exitRequested(); });
    grid->addWidget(exitBtn, 0, 2, 2, 1);
    grid->setColumnStretch(3, 1);   // 多余宽度吃在尾列:按钮组靠左贴齐

    m_caption = new QLabel(this);
    m_caption->setObjectName("filmCaption");   // #230:题注纯白大字,样式在应用级 QSS
    m_caption->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
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
    m_currentPath = current;
    if (paths == m_allPaths) {
        // 同一份全量表再灌一遍(网格筛选切换会触发 fileCountChanged,但条已
        // 不跟筛选走):不做重建,只对账蓝框/居中/题注,免得滚动位置闪跳
        locateCurrent();
        return;
    }
    m_allPaths = paths;
    refilter();
}

void FilmStrip::syncCurrent(const QString& path) {
    m_currentPath = path;
    if (path.isEmpty() || currentPath() == path) return;
    locateCurrent();
}

// 在显示列表里定位当前文件;找不到(被类别勾选滤掉)就收蓝框、藏题注
void FilmStrip::locateCurrent() {
    int row = -1;
    if (!m_currentPath.isEmpty()) {
        for (int i = 0; i < m_model->rowCount(); ++i)
            if (m_model->pathAt(i) == m_currentPath) { row = i; break; }
    }
    applyCurrent(row, true);
    requestVisibleThumbs();
}

// #226:按类别勾选从全量表 m_allPaths 生成显示列表。其他类型(文本/文档/
// 可执行…)没有按钮管、始终显示 —— "所有文件都参与进来"的兜底。
void FilmStrip::refilter() {
    QStringList shown;
    shown.reserve(m_allPaths.size());
    for (const QString& p : m_allPaths) {
        switch (catOf(p)) {
        case CatImage: if (!m_showImg) continue; break;
        case CatVideo: if (!m_showVid) continue; break;
        case CatAudio: if (!m_showAud) continue; break;
        case CatOther: if (!m_showOth) continue; break;   // #228:第四类归队
        }
        shown << p;
    }
    m_requested.clear();
    m_pressRow = -1;
    m_panning  = false;
    m_hoverRow = -1;
    m_model->reset(shown);
    horizontalScrollBar()->setValue(0);
    locateCurrent();
}

void FilmStrip::applyCurrent(int row, bool center) {
    if (m_currentRow != row) {
        m_currentRow = row;
        viewport()->update();   // 蓝框从旧位置挪到新位置
        updateCaption();
    }
    if (row >= 0 && center) centerRow(row);
}

// ── #220 当前项强制居中(首尾张也真居中) ──
// scrollTo(PositionAtCenter) 受滚动范围 [0, 内容宽-视口] 钳制:第一张/最后一张
// 只能贴边,永远到不了正中。这里手工算滚动值,并把范围向两侧各扩 pad —— 越界
// 部分是留白(条底色透出),第一张左边空着正是用户要的。负滚动值 QAbstractScrollArea
// 原生支持,indexAt 的坐标换算(加滚动值)对负值同样成立,点击/悬停不受影响。
// 对齐目标按条几何中心算:右边距(kBtnZone+10)比左边距(8)宽,目标值里补回差半。
void FilmStrip::centerRow(int row) {
    const int n = m_model->rowCount();
    if (row < 0 || row >= n) return;
    const int rowW = kThumbW + kGap;
    const int vw = viewport()->width();
    if (vw <= 0) return;
    QScrollBar* h = horizontalScrollBar();
    const int asym = kBtnZone + 2;   // 右边距 - 左边距
    const int pad = qMax(8, (vw - kThumbW) / 2 + asym / 2);
    const int contentW = n * rowW - kGap;
    h->setRange(-pad, qMax(0, contentW + pad - vw));
    h->setValue(row * rowW + kThumbW / 2 - (vw + asym) / 2);
}

// QListView 每次重排版都会把水平范围重设回 [0, 内容宽-视口],把我们的 pad 抹掉
// (首尾张又贴边)。跟着重居中一次,任何时机的范围重置都被这里拉回来。
void FilmStrip::updateGeometries() {
    QListView::updateGeometries();
    if (m_currentRow >= 0) centerRow(m_currentRow);
}

void FilmStrip::updateCaption() {
    const int n = m_model->rowCount();
    if (m_currentRow < 0 || n <= 0) { m_caption->hide(); return; }
    const QString p = m_model->pathAt(m_currentRow);
    // #230:题注带上文件大小(单次 stat,只在当前项变化时发生一次)
    m_caption->setText(gazeTr("%1 · %2 · %3 / %4")
        .arg(QFileInfo(p).fileName())
        .arg(formatSize(QFileInfo(p).size()))
        .arg(m_currentRow + 1).arg(n));
    m_caption->show();
}

// 懒取缩略图:可见区 ±1 张,按均匀条目宽从滚动值直接算行号(ScrollPerPixel 下精确,
// 不必 indexAt 往回猜)。已入队/已缓存的不重复入队。
// #225:音频/文本/可执行/RAW 出不了缩略图(#140 铁令:RAW 绝不进缩略图管线),
// 不入队白耗工 —— 这些格子由 delegate 直接画文件名。
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
        const Cat c = catOf(p);
        const bool thumbable = c == CatVideo
            || (c == CatImage
                && !RAW_EXTS.count(QFileInfo(p).suffix().toLower().prepend(QLatin1Char('.'))));
        if (!thumbable) continue;
        m_requested.insert(p);
        Thumbnailer::instance().enqueue(p, kThumbW, c == CatVideo);
    }
}

// 2026-09-05 用户令:条上滚动不再只滚画廊 —— 画廊与图片一起滚:每滚一格
// 发 stepRequested 给主窗(navigateSelection 同一条链),选区一变,预览换图,
// 蓝框经 syncCurrent→centerRow 拉回正中,当前图恒在画廊中间。触控板像素
// 滚动按累积 76px(=一格)折算一步,保留平滑手感。
void FilmStrip::wheelEvent(QWheelEvent* e) {
    if (!e->pixelDelta().isNull()) {
        const QPoint d = e->pixelDelta();
        m_pxAcc += (d.x() != 0 ? d.x() : d.y());
    } else {
        const int dy = e->angleDelta().y();
        if (dy % 120 == 0 && dy != 0) {
            const int notches = dy / 120;
            for (int i = 0; i < qAbs(notches); ++i) emit stepRequested(notches > 0 ? -1 : 1);
            e->accept();
            return;
        }
        m_pxAcc += -dy / 4;   // 高分辨率滚轮
    }
    while (qAbs(m_pxAcc) >= kStepPx) {
        const int s = m_pxAcc > 0 ? -1 : 1;
        m_pxAcc -= s * kStepPx;
        emit stepRequested(s);
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
    m_caption->setGeometry(8, height() - kCaptionH + 4, width() - 20 - kBtnZone, 16);
    m_btnBar->setGeometry(width() - kBtnZone - 6, 4, kBtnZone, 62);
    requestVisibleThumbs();
}
