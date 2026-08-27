#include "sortheader.h"
#include "constants.h"
#include <QLabel>
#include <QMenu>
#include <QContextMenuEvent>

SortHeader::SortHeader(QWidget* parent) : QWidget(parent) {
    setFixedHeight(26);
    setStyleSheet("background:" C_TOOLBAR ";border-bottom:1px solid " C_SEPARATOR ";");

    m_layout = new QHBoxLayout(this);
    m_layout->setContentsMargins(6, 0, 6, 0);
    m_layout->setSpacing(0);

    struct { int id; QString text; } cols[] = {
        {SORT_NAME,   "文件名"},
        {SORT_SIZE,   "大小"},
        {SORT_TYPE,   "类型"},
        {SORT_EXT,    "扩展名"},
        {SORT_CDATE,  "创建日期"},
        {SORT_MDATE,  "修改日期"},
        {SORT_EXIF,   "EXIF日期"},
    };

    QString btnStyle =
        "QPushButton{background:transparent;color:#FFFFFF;border:none;"
        "padding:2px 8px;font-size:11px;text-align:left;border-radius:4px;}"
        "QPushButton:hover{color:" C_TEXT ";background:" C_PANE_HDR ";}";

    for (auto& c : cols) {
        auto* btn = new QPushButton(c.text);
        btn->setStyleSheet(btnStyle);
        btn->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        connect(btn, &QPushButton::clicked, this, [this, id = c.id]() {
            onColumnClicked(id);
        });
        m_layout->addWidget(btn, 1);
        m_columns.append({c.id, btn});
    }

    updateArrows();

    // 右键"配置列":勾选列显隐
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this,
            [this](const QPoint& pos) {
        QMenu menu(this);
        for (auto& c : m_columns) {
            QAction* a = menu.addAction(c.btn->text(), this, [c]() {
                c.btn->setVisible(!c.btn->isVisible());
            });
            a->setCheckable(true);
            a->setChecked(c.btn->isVisible());
        }
        menu.exec(mapToGlobal(pos));
    });
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
        QString text = c.btn->text();
        if (text.endsWith(" ▲") || text.endsWith(" ▼"))
            text = text.left(text.length() - 2);
        if (c.id == m_currentCol)
            text += m_ascending ? " ▲" : " ▼";
        c.btn->setText(text);
    }
}

void SortHeader::setColumnWidths(const QList<int>& widths) {
    for (int i = 0; i < widths.size() && i < m_columns.size(); ++i) {
        m_columns[i].btn->setFixedWidth(widths[i]);
    }
}
