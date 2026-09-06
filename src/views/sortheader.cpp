#include "sortheader.h"
#include "constants.h"
#include "i18n.h"
#include <QLabel>
#include <QMenu>
#include <QContextMenuEvent>
#include <QResizeEvent>

// #267 详细列表定宽列(真源):大小/类型/扩展名/创建日期/修改日期/EXIF日期。
// FileGrid::detailColW 绘制用同一张表,两处永不漂移
int SortHeader::detailColWidth(int i) {
    static const int W[6] = { 72, 96, 56, 112, 112, 112 };
    return (i >= 0 && i < 6) ? W[i] : 0;
}

// #3/#5 用户令(2026-09-05):列宽不再是死表 ——
//   · 拖宽文件页:增量在"名称 + 各列"之间均摊(过去全喂给名称列,大小/类型
//     纹丝不动,名称列一枝独秀地过宽);
//   · 拖窄:名称列保底 200px(约 25 字符,画侧右端省略号),其余列从基准向
//     最小值等比压缩为名称让路;再窄(最小值之和都放不下)才允许名称跌破保底。
// 基准/最小值:日期列 11px 字体 "yyyy/M/d HH:mm" 需 ~95px,最小 84 仍可读。
void SortHeader::dynDetailWidths(int rowW, const bool vis[6], int out[6]) {
    static const int B[6] = { 72, 96, 56, 112, 112, 112 };
    static const int M[6] = { 56, 64, 40,  84,  84,  84 };
    constexpr int kNameMin = 200;
    int n = 0, S = 0, Smin = 0;
    for (int i = 0; i < 6; ++i) {
        out[i] = vis[i] ? B[i] : 0;
        if (!vis[i]) continue;
        ++n; S += B[i]; Smin += M[i];
    }
    if (n == 0) return;
    // 名称实际宽度 = rowW - 33(28 图标+偏移 与 5 间隙,见 paintDetailsRow)。
    // 名称 200px 是绝对优先项:不够宽时各列按基准**等比**压下来让位
    // (2026-09-06 复查:旧公式窄窗口下连最小值档都进不去,名称直接被压成 0,
    // 整列文件名消失——用户截图实锤)。32px 是单列的可读下限(再窄就隐藏级了)。
    const int availMax = qMax(0, rowW - 33 - kNameMin);
    if (S <= availMax) {
        const int extra = availMax - S;
        const int share = extra / (n + 1);   // 名称也是一份:它弹性吸收余数
        for (int i = 0; i < 6; ++i)
            if (vis[i]) out[i] = B[i] + share;
    } else if (availMax > 32 * n) {
        const double t = double(availMax) / double(S);
        for (int i = 0; i < 6; ++i)
            if (vis[i]) out[i] = qMax(32, int(B[i] * t + 0.5));
    } else {
        for (int i = 0; i < 6; ++i)
            if (vis[i]) out[i] = 32;         // 物理极限:每列 32,名称吃剩余
    }
}

bool SortHeader::detailColumnVisible(int i) const {
    // isHidden 而非 isVisible:表头尚未显示(启动期)时用户意图不受祖先链影响
    return i >= 0 && i + 1 < m_columns.size() && !m_columns[i + 1].btn->isHidden();
}

void SortHeader::setDetailLead(int w) {
    w = qMax(0, w);
    if (m_leadW == w) return;
    m_leadW = w;
    if (m_detailMode) m_leadSpacer->setFixedWidth(w);
}

void SortHeader::setDetailTail(int w) {
    w = qMax(0, w);
    if (m_tailW == w) return;
    m_tailW = w;
    if (m_detailMode) m_tailSpacer->setFixedWidth(w);
}

// 列钮尺寸变化:小箭头跟随钉在右缘垂直居中
bool SortHeader::eventFilter(QObject* obj, QEvent* event) {
    if (event->type() == QEvent::Resize) {
        for (const auto& c : m_columns)
            if (c.btn == obj && c.arrow)
                c.arrow->move(c.btn->width() - 13, (c.btn->height() - 10) / 2);
    }
    return QWidget::eventFilter(obj, event);
}

// 非详细态的列宽分担(2026-09-06 用户令"大家的列都变宽,而且从一开始就
// 差不多宽"):全部可见列等权,初始与拖宽都均分。此前的名称加权(260)让它
// 一出生就比别人宽一大截,与诉求相悖。详细态不走这里(定宽列方案)。
void SortHeader::applySharedStretch() {
    for (int i = 0; i < m_columns.size(); ++i)
        m_layout->setStretchFactor(m_columns[i].btn, 1);
}

void SortHeader::setDetailMode(bool on) {
    if (m_detailMode == on) return;
    m_detailMode = on;
    m_leadSpacer->setVisible(on);
    m_tailSpacer->setVisible(on);
    m_leadSpacer->setFixedWidth(m_leadW);
    m_tailSpacer->setFixedWidth(m_tailW);
    for (int i = 1; i < m_columns.size(); ++i) {   // i≥1:大小起为定宽列(i-1 对应 detailColWidth)
        QPushButton* b = m_columns[i].btn;
        if (on) {
            b->setFixedWidth(detailColWidth(i - 1));
            m_layout->setStretchFactor(b, 0);
        } else {
            b->setMinimumWidth(0);
            b->setMaximumWidth(QWIDGETSIZE_MAX);
        }
    }
    if (!on) applySharedStretch();
    // 名称列在详细态恢复独占弹性(动态列宽由 setDetailWidths 推送)
    m_layout->setStretchFactor(m_columns[0].btn, 1);
    updateGeometry();
}

// 表头列钮同步动态宽(FileGrid::updateDetailColumns 推):名称列不吃这份,
// 它继续保持弹性,吸收行宽与各列定宽的差值
void SortHeader::setDetailWidths(const int w[6]) {
    if (!m_detailMode) return;
    for (int i = 1; i < m_columns.size(); ++i) {
        const int want = qMax(0, w[i - 1]);
        if (m_columns[i].btn->maximumWidth() != want)
            m_columns[i].btn->setFixedWidth(want);
    }
}

void SortHeader::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    // 表头宽变了:尾垫片要重算才能继续与网格行右缘对齐(FileGrid::updateDetailColumns 推回)
    if (m_detailMode) emit detailColumnsEdited();
}

SortHeader::SortHeader(QWidget* parent) : QWidget(parent) {
    setFixedHeight(26);
    // 自身底色/下边线在应用级 QSS(SortHeader 规则,#89 收敛)

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 0, 6, 0);
    m_layout->setSpacing(0);

    // #267:详细态头垫片(名称文字起点与网格行对齐);非详细态宽 0 且隐藏
    m_leadSpacer = new QWidget(this);
    m_leadSpacer->setFixedWidth(0);
    m_leadSpacer->setVisible(false);
    m_layout->addWidget(m_leadSpacer);

    struct { int id; QString text; } cols[] = {
        {SORT_NAME,   gazeTr("文件名")},
        {SORT_SIZE,   gazeTr("大小")},
        {SORT_TYPE,   gazeTr("类型")},
        {SORT_EXT,    gazeTr("扩展名")},
        {SORT_CDATE,  gazeTr("创建日期")},
        {SORT_MDATE,  gazeTr("修改日期")},
        {SORT_EXIF,   gazeTr("EXIF日期")},
    };

    // 列钮样式在应用级 QSS(SortHeader QPushButton 规则,#89 收敛)
    for (auto& c : cols) {
        auto* btn = new QPushButton(c.text);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        btn->installEventFilter(this);   // 尺寸变化 → 小箭头跟随右缘
        // 排序方向箭头:独立小标签(灰 C_SB_ARROW/8px,与筛选框箭头同款观感),
        // 不再拼进按钮文字(2026-09-06 用户令)
        auto* arrow = new QLabel(btn);
        arrow->setStyleSheet(QString("QLabel{color:%1;background:transparent;"
                                     "font-size:8px;}")
                                 .arg(QString::fromUtf8(C_SB_ARROW)));
        arrow->setFixedSize(10, 10);
        arrow->setAlignment(Qt::AlignCenter);
        arrow->move(0, 0);
        arrow->show();
        connect(btn, &QPushButton::clicked, this, [this, id = c.id]() {
            onColumnClicked(id);
        });
        // 名称列独占伸缩(stretch=1):其余按钮停在自然宽,多余空间全给文件名。
        // 详细态下其余按钮转为定宽(见 setDetailMode),名称依旧吸收剩余
        m_layout->addWidget(btn, c.id == SORT_NAME ? 1 : 0);
        m_columns.append({c.id, btn, arrow});
    }

    // #267:详细态尾垫片(列区右缘对齐网格行右缘);非详细态宽 0 且隐藏
    m_tailSpacer = new QWidget(this);
    m_tailSpacer->setFixedWidth(0);
    m_tailSpacer->setVisible(false);
    m_layout->addWidget(m_tailSpacer);

    applySharedStretch();   // 默认非详细态:各列共同分担增量
    updateArrows();

    // 右键"配置列":勾选列显隐(详细态变化会改列宽,通知网格重推对齐几何)
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        QMenu menu(this);
        for (auto& c : m_columns) {
            if (m_detailMode && c.id == SORT_NAME) continue;   // 详细态名称列是行主体,不允许隐藏
            QAction* a = menu.addAction(c.btn->text(), this, [this, c]() {
                c.btn->setVisible(!c.btn->isHidden());
                if (m_detailMode) emit detailColumnsEdited();
            });
            a->setCheckable(true);
            a->setChecked(!c.btn->isHidden());
        }
        menu.exec(mapToGlobal(pos));
    });
}

// 程序侧排序(启动默认/排序菜单/名称顺序切换)后的方向同步:
// 此前箭头只在点表头时更新,构造默认画着"修改日期"——启动按文件名排序、
// 表头却显示修改日期,两张皮(2026-09-06 用户报)
void SortHeader::setSortIndicator(int colId, bool ascending) {
    if (m_currentCol == colId && m_ascending == ascending) return;
    m_currentCol = colId;
    m_ascending = ascending;
    updateArrows();
}

void SortHeader::onColumnClicked(int colId) {
    if (m_currentCol == colId)
        m_ascending = !m_ascending;
    else {
        m_currentCol = colId;
        m_ascending = true;
    }
    updateArrows();
    emit sortChanged(m_currentCol, m_ascending);
}

void SortHeader::updateArrows() {
    for (auto& c : m_columns) {
        // 方向箭头画在钮内右缘的小标签上,文字保持干净
        if (c.arrow) {
            c.arrow->setText(c.id == m_currentCol
                                 ? (m_ascending ? gazeTr("▲") : gazeTr("▼"))
                                 : QString());
            c.arrow->move(c.btn->width() - 13, (c.btn->height() - 10) / 2);
        }
    }
}

void SortHeader::setColumnWidths(const QList<int>& widths) {
    for (int i = 0; i < widths.size() && i < m_columns.size(); ++i) {
        m_columns[i].btn->setFixedWidth(widths[i]);
    }
}
