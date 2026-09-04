#pragma once
// ═══════════════════════════════════════════
// 分类筛选器面板(任务 #242)—— 文件树下方,视图菜单开关
//   · 两个条件维度:颜色标记(红~蓝色块钮) + 文件类型(六类勾选框)
//   · 维度内多选 = 任一命中;维度间「任一(OR)/全部(AND)」二选一
//   · 范围:当前目录 / 当前目录(递归) / 全部标记文件(颜色标记库)
//   · 条件真源在本面板;勾选即落盘 Filter/*,面板重开/重启后恢复
//   · 面板关闭 = 条件不生效(MainWindow 在 applyPaneVisibility 关分支清筛选)
// ═══════════════════════════════════════════
#include <QWidget>
#include <QToolButton>
#include <QPushButton>
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QButtonGroup>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QPixmap>
#include <QIcon>
#include "constants.h"
#include "i18n.h"
#include "labelstore.h"
#include "settings.h"
#include "filegrid.h"      // MultiFilterSpec

class FilterPanel : public QWidget {
    Q_OBJECT
public:
    explicit FilterPanel(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(6, 4, 6, 4);
        root->setSpacing(4);

        // ── 颜色维:5 个圆形色块钮(checkable,外圈勾选态加亮)──
        auto* colorRow = new QHBoxLayout;
        colorRow->setSpacing(4);
        static const char* const colorNames[5] =
            { QT_TR_NOOP("红"), QT_TR_NOOP("橙"), QT_TR_NOOP("黄"), QT_TR_NOOP("绿"), QT_TR_NOOP("蓝") };
        for (int c = 1; c <= 5; ++c) {
            auto* b = new QToolButton;
            b->setCheckable(true);
            b->setFixedSize(24, 24);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(gazeTr(colorNames[c - 1]));
            QPixmap pix(14, 14);
            pix.fill(LabelStore::colorValue(c));
            b->setIcon(QIcon(pix));
            b->setIconSize(QSize(14, 14));
            colorRow->addWidget(b);
            m_colorBtns[c - 1] = b;
        }
        colorRow->addStretch(1);
        root->addLayout(colorRow);

        // ── 类型维:6 个勾选框,两列三行(与工具栏格式筛选同一套类名词)──
        auto* grid = new QGridLayout;
        grid->setHorizontalSpacing(6);
        grid->setVerticalSpacing(2);
        static const char* const catNames[6] = {
            QT_TR_NOOP("图像"), QT_TR_NOOP("视频"),
            QT_TR_NOOP("音频"), QT_TR_NOOP("文档"),
            QT_TR_NOOP("可执行"), QT_TR_NOOP("压缩")
        };
        for (int t = 0; t < 6; ++t) {
            auto* cb = new QCheckBox(gazeTr(catNames[t]));
            cb->setCursor(Qt::PointingHandCursor);
            grid->addWidget(cb, t / 2, t % 2);
            m_catBoxes[t] = cb;
        }
        root->addLayout(grid);

        // ── 维度间与/或 + 清除 ──
        auto* modeRow = new QHBoxLayout;
        modeRow->setSpacing(4);
        m_orBtn = new QToolButton;
        m_orBtn->setText(gazeTr("任一"));
        m_orBtn->setCheckable(true);
        m_orBtn->setToolTip(gazeTr("任一:颜色或类型条件命中其一即显示"));
        m_andBtn = new QToolButton;
        m_andBtn->setText(gazeTr("全部"));
        m_andBtn->setCheckable(true);
        m_andBtn->setToolTip(gazeTr("全部:颜色和类型条件须同时命中"));
        auto* modeGrp = new QButtonGroup(this);
        modeGrp->setExclusive(true);
        modeGrp->addButton(m_orBtn);
        modeGrp->addButton(m_andBtn);
        modeRow->addWidget(m_orBtn);
        modeRow->addWidget(m_andBtn);
        modeRow->addStretch(1);
        auto* clearBtn = new QPushButton(gazeTr("清除筛选"));
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setToolTip(gazeTr("取消全部勾选(范围保持不变)"));
        modeRow->addWidget(clearBtn);
        root->addLayout(modeRow);

        // ── 范围 ──
        m_scope = new QComboBox;
        m_scope->addItem(gazeTr("当前目录"));
        m_scope->addItem(gazeTr("当前目录(递归)"));
        m_scope->addItem(gazeTr("全部标记文件"));
        m_scope->setToolTip(gazeTr(
            "全部标记文件 = 在整个颜色标记库中搜索(跨目录);格式条件在其结果上生效"));
        root->addWidget(m_scope);

        // ── 命中计数(弱文字;fileCountChanged 驱动)──
        m_hitLabel = new QLabel;
        m_hitLabel->setObjectName("filterHitLabel");
        root->addWidget(m_hitLabel);
        root->addStretch(1);

        // ── 恢复上次条件(静默:先设状态后接线,避免构造期乱发)──
        AppSettings& st = AppSettings::instance();
        const QStringList cs = st.get("Filter/colors", QString()).toString()
                                   .split(',', Qt::SkipEmptyParts);
        for (const QString& s : cs) {
            const int v = s.toInt();
            if (v >= 1 && v <= 5) m_colorBtns[v - 1]->setChecked(true);
        }
        const QStringList ts = st.get("Filter/cats", QString()).toString()
                                   .split(',', Qt::SkipEmptyParts);
        for (const QString& s : ts) {
            const int v = s.toInt();
            if (v >= 0 && v <= 5) m_catBoxes[v]->setChecked(true);
        }
        (st.get("Filter/andMode", false).toBool() ? m_andBtn : m_orBtn)->setChecked(true);
        m_scope->setCurrentIndex(qBound(0, st.get("Filter/scope", 0).toInt(), 2));

        applyTheme();

        // ── 接线(状态就位之后)──
        for (int c = 1; c <= 5; ++c)
            connect(m_colorBtns[c - 1], &QToolButton::toggled,
                    this, [this](bool) { if (!m_updating) emitChange(); });
        for (int t = 0; t < 6; ++t)
            connect(m_catBoxes[t], &QCheckBox::toggled,
                    this, [this](bool) { if (!m_updating) emitChange(); });
        connect(m_orBtn,  &QToolButton::toggled, this, [this](bool on) { if (on) emitChange(); });
        connect(m_andBtn, &QToolButton::toggled, this, [this](bool on) { if (on) emitChange(); });
        connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int) { emitChange(); });
        connect(clearBtn, &QPushButton::clicked, this, [this]() {
            // 十个勾一起撤:置哨兵拦住中间态,收尾只发一次
            m_updating = true;
            for (int c = 1; c <= 5; ++c) m_colorBtns[c - 1]->setChecked(false);
            for (int t = 0; t < 6; ++t)  m_catBoxes[t]->setChecked(false);
            m_updating = false;
            emitChange();
        });
    }

    // 当前勾选 → spec(active 自算;范围走 scope())
    MultiFilterSpec spec() const {
        MultiFilterSpec s;
        for (int c = 1; c <= 5; ++c)
            if (m_colorBtns[c - 1]->isChecked()) s.colors.insert(c);
        for (int t = 0; t < 6; ++t)
            if (m_catBoxes[t]->isChecked()) s.cats.insert(t);
        s.andMode = m_andBtn->isChecked();
        s.active = !s.colors.isEmpty() || !s.cats.isEmpty();
        return s;
    }
    int scope() const { return m_scope->currentIndex(); }

    void setHitCount(int n) { m_hitLabel->setText(gazeTr("显示 %1 项").arg(n)); }

    // #248 同款:构造期按主题求值一次,切主题由 applyThemeSurfaces 重灌
    void applyTheme() {
        const QString ss = QString::fromUtf8(
            "QToolButton{background:transparent;color:%1;border:1px solid %2;"
            "border-radius:4px;padding:2px 8px;font-size:12px;}"
            "QToolButton:hover{background:%3;}"
            "QToolButton:checked{border-color:%4;color:%4;font-weight:bold;}"
            "QPushButton{background:transparent;color:%1;border:1px solid %2;"
            "border-radius:4px;padding:2px 8px;font-size:12px;}"
            "QPushButton:hover{background:%3;}"
            "QCheckBox{color:%1;font-size:12px;spacing:4px;background:transparent;}"
            "QCheckBox::indicator{width:13px;height:13px;}"
            "QComboBox{background:transparent;color:%1;border:1px solid %2;"
            "border-radius:4px;padding:2px 4px;font-size:12px;}"
            "QLabel#filterHitLabel{color:%5;font-size:11px;background:transparent;}")
            .arg(C_TEXT, C_SEPARATOR, C_CARD_HOVER, C_ACCENT, C_TEXT_FAINT);
        setStyleSheet(ss);
        // 色块钮:圆形描边,勾选=蓝圈;样式表不能混用,单独逐钮灌
        for (int c = 1; c <= 5; ++c) {
            m_colorBtns[c - 1]->setStyleSheet(QString::fromUtf8(
                "QToolButton{background:transparent;border:1px solid %1;border-radius:12px;}"
                "QToolButton:checked{border:2px solid %2;}")
                .arg(C_SEPARATOR, C_ACCENT));
        }
    }

signals:
    void conditionsChanged(int scope);

private:
    bool anyChecked() const {
        for (int c = 1; c <= 5; ++c) if (m_colorBtns[c - 1]->isChecked()) return true;
        for (int t = 0; t < 6; ++t)  if (m_catBoxes[t]->isChecked()) return true;
        return false;
    }
    void emitChange() {
        // 落盘(面板重开/重启恢复);勾选是低频交互,逐键 set 无 perf 顾虑
        QStringList cs, ts;
        for (int c = 1; c <= 5; ++c)
            if (m_colorBtns[c - 1]->isChecked()) cs << QString::number(c);
        for (int t = 0; t < 6; ++t)
            if (m_catBoxes[t]->isChecked()) ts << QString::number(t);
        AppSettings& st = AppSettings::instance();
        st.set("Filter/colors", cs.join(','));
        st.set("Filter/cats", ts.join(','));
        st.set("Filter/andMode", m_andBtn->isChecked());
        st.set("Filter/scope", m_scope->currentIndex());
        emit conditionsChanged(m_scope->currentIndex());
    }

    QToolButton* m_colorBtns[5] = {};
    QCheckBox*   m_catBoxes[6]  = {};
    QToolButton* m_orBtn  = nullptr;
    QToolButton* m_andBtn = nullptr;
    QComboBox*   m_scope  = nullptr;
    QLabel*      m_hitLabel = nullptr;
    bool         m_updating = false;   // 程序化批量改勾选中(拦中间态 emit)
};
