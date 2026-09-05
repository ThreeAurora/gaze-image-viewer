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
#include <QMessageBox>
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
        setObjectName(QStringLiteral("filterPanel"));
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(8, 6, 8, 6);
        root->setSpacing(6);

        // ── 模式行(2026-09-05 用户令):「颜色标记 / 类型」两种扁平切换,
        //    一次只织入一个维度,不再同屏两套条件 + 与/或 ──
        auto* modeRow = new QHBoxLayout;
        modeRow->setSpacing(4);
        auto mkMode = [&](const QString& text, const QString& tip, int id) {
            auto* b = new QToolButton;
            b->setText(text);
            b->setToolTip(tip);
            b->setCheckable(true);
            b->setCursor(Qt::PointingHandCursor);
            b->setObjectName(QStringLiteral("filterModeBtn"));
            b->setFocusPolicy(Qt::TabFocus);
            modeRow->addWidget(b, 1);
            connect(b, &QToolButton::toggled, this, [this, id](bool on) {
                if (!on) return;
                m_mode = id;
                AppSettings::instance().set("Filter/mode", id);
                syncModeVisibility();
                if (!m_updating) emitChange();
            });
            return b;
        };
        m_colorModeBtn = mkMode(gazeTr("颜色标记"),
                                gazeTr("按颜色标记筛选(维度内多选=任一命中)"), 0);
        m_typeModeBtn  = mkMode(gazeTr("类型"),
                                gazeTr("按文件类型筛选(维度内多选=任一命中)"), 1);
        // 状态先于接线恢复(下方),这里的互斥用手动压:同一行只留一个按下
        connect(m_colorModeBtn, &QToolButton::toggled, this, [this](bool on) {
            if (on && m_typeModeBtn->isChecked()) { m_updating = true; m_typeModeBtn->setChecked(false); m_updating = false; }
        });
        connect(m_typeModeBtn, &QToolButton::toggled, this, [this](bool on) {
            if (on && m_colorModeBtn->isChecked()) { m_updating = true; m_colorModeBtn->setChecked(false); m_updating = false; }
        });
        root->addLayout(modeRow);

        // ── 颜色维:5 个圆形色块钮(28px 圆形,勾选=蓝圈)──
        m_colorRow = new QWidget;
        auto* colorRow = new QHBoxLayout(m_colorRow);
        colorRow->setContentsMargins(0, 0, 0, 0);
        colorRow->setSpacing(6);
        static const char* const colorNames[5] =
            { QT_TR_NOOP("红"), QT_TR_NOOP("橙"), QT_TR_NOOP("黄"), QT_TR_NOOP("绿"), QT_TR_NOOP("蓝") };
        for (int c = 1; c <= 5; ++c) {
            auto* b = new QToolButton;
            b->setObjectName(QStringLiteral("filterColorBtn"));
            b->setCheckable(true);
            b->setFixedSize(28, 28);
            b->setCursor(Qt::PointingHandCursor);
            b->setToolTip(gazeTr(colorNames[c - 1]));
            // 圆形色块:用抗锯齿画整圆,不再方块贴进方钮
            QPixmap pix(20, 20);
            pix.fill(Qt::transparent);
            {
                QPainter p(&pix);
                p.setRenderHint(QPainter::Antialiasing);
                p.setPen(Qt::NoPen);
                p.setBrush(LabelStore::colorValue(c));
                p.drawEllipse(1, 1, 18, 18);
                p.end();
            }
            b->setIcon(QIcon(pix));
            b->setIconSize(QSize(20, 20));
            colorRow->addWidget(b);
            colorRow->addStretch(1);
            m_colorBtns[c - 1] = b;
        }
        root->addWidget(m_colorRow);

        // ── 类型维:6 个勾选框(3 行 2 列)──
        m_typeGrid = new QWidget;
        auto* grid = new QGridLayout(m_typeGrid);
        grid->setContentsMargins(0, 0, 0, 0);
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
        root->addWidget(m_typeGrid);

        // ── 范围 + 清除 ──
        auto* scopeRow = new QHBoxLayout;
        scopeRow->setSpacing(6);
        m_scope = new QComboBox;
        m_scope->addItem(gazeTr("当前目录"));
        m_scope->addItem(gazeTr("当前目录(递归)"));
        m_scope->addItem(gazeTr("全部标记文件"));
        m_scope->setToolTip(gazeTr(
            "全部标记文件 = 在整个颜色标记库中搜索(跨目录);格式条件在其结果上生效"));
        scopeRow->addWidget(m_scope, 1);
        auto* clearBtn = new QToolButton;
        clearBtn->setText(gazeTr("清除"));
        clearBtn->setCursor(Qt::PointingHandCursor);
        clearBtn->setToolTip(gazeTr("取消全部勾选(范围保持不变)"));
        scopeRow->addWidget(clearBtn);
        root->addLayout(scopeRow);

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
        m_mode = qBound(0, st.get("Filter/mode", 0).toInt(), 1);
        (m_mode == 0 ? m_colorModeBtn : m_typeModeBtn)->setChecked(true);
        syncModeVisibility();
        m_scope->setCurrentIndex(qBound(0, st.get("Filter/scope", 0).toInt(), 2));
        m_lastScope = m_scope->currentIndex();   // 构造期恢复不弹警告(先设状态后接线)

        // ── 接线(状态就位之后)──
        for (int c = 1; c <= 5; ++c)
            connect(m_colorBtns[c - 1], &QToolButton::toggled,
                    this, [this](bool) { if (!m_updating) emitChange(); });
        for (int t = 0; t < 6; ++t)
            connect(m_catBoxes[t], &QCheckBox::toggled,
                    this, [this](bool) { if (!m_updating) emitChange(); });
        connect(m_scope, &QComboBox::currentIndexChanged, this, [this](int idx) {
            // 特殊范围两档先警告(2026-09-05 用户令):文件过多可能卡死,询问后才生效;
            // 拒绝=回退上次生效档(回退触发再进时 idx==m_lastScope 不再弹)。
            // 构造期恢复走"先设状态后接线"不经过这里,重启恢复不弹。
            if ((idx == 1 || idx == 2) && idx != m_lastScope) {
                const QString msg = (idx == 1)
                    ? gazeTr("「当前目录(递归)」要扫描全部子目录，文件很多时可能卡顿。确定使用吗？")
                    : gazeTr("「全部标记文件」要在整个颜色标记库中搜索，文件很多时可能卡顿。确定使用吗？");
                // 2026-09-05 用户令:警示样式 + 中文按钮。Yes/No 英文钮不认路,
                // Warning 图标+默认焦点落在"取消",防手滑直接回车放行。
                QMessageBox box(QMessageBox::Warning, gazeTr("特殊范围确认"), msg,
                                QMessageBox::NoButton, this);
                QPushButton* okBtn = box.addButton(gazeTr("确定"), QMessageBox::YesRole);
                box.addButton(gazeTr("取消"), QMessageBox::NoRole);
                box.setDefaultButton(QMessageBox::No);
                box.exec();
                if (box.clickedButton() != okBtn) {
                    m_scope->setCurrentIndex(m_lastScope);
                    return;
                }
            }
            m_lastScope = idx;
            emitChange();
        });
        connect(clearBtn, &QToolButton::clicked, this, [this]() {
            // 当期维度的勾一起撤:置哨兵拦住中间态,收尾只发一次
            m_updating = true;
            for (int c = 1; c <= 5; ++c) m_colorBtns[c - 1]->setChecked(false);
            for (int t = 0; t < 6; ++t)  m_catBoxes[t]->setChecked(false);
            m_updating = false;
            emitChange();
        });
    }

    // 当前模式勾选 → spec(active 自算;范围走 scope())。
    // 只收当前维度:另一维度的历史勾选保持在 ini 里,不暗中参与筛选
    MultiFilterSpec spec() const {
        MultiFilterSpec s;
        if (m_mode == 0) {
            for (int c = 1; c <= 5; ++c)
                if (m_colorBtns[c - 1]->isChecked()) s.colors.insert(c);
        } else {
            for (int t = 0; t < 6; ++t)
                if (m_catBoxes[t]->isChecked()) s.cats.insert(t);
        }
        s.andMode = false;
        s.active = !s.colors.isEmpty() || !s.cats.isEmpty();
        return s;
    }
    int scope() const { return m_scope->currentIndex(); }

    void setHitCount(int n) { m_hitLabel->setText(gazeTr("显示 %1 项").arg(n)); }

    // 整表样式迁入应用级 QSS(QWidget#filterPanel 规则组,#89 收敛):
    // 切主题由 Theme::applyLive 重设全局表自动跟上,不再需要重灌钩子

signals:
    void conditionsChanged(int scope);

private:
    void syncModeVisibility() {
        if (m_colorRow) m_colorRow->setVisible(m_mode == 0);
        if (m_typeGrid) m_typeGrid->setVisible(m_mode == 1);
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
        st.set("Filter/mode", m_mode);
        st.set("Filter/scope", m_scope->currentIndex());
        emit conditionsChanged(m_scope->currentIndex());
    }

    QToolButton* m_colorModeBtn = nullptr;
    QToolButton* m_typeModeBtn  = nullptr;
    QWidget*     m_colorRow     = nullptr;   // 颜色行容器(模式切换显隐)
    QWidget*     m_typeGrid     = nullptr;
    QToolButton* m_colorBtns[5] = {};
    QCheckBox*   m_catBoxes[6]  = {};
    QComboBox*   m_scope  = nullptr;
    QLabel*      m_hitLabel = nullptr;
    int          m_mode = 0;          // 0=颜色标记 1=类型(持久化 Filter/mode)
    int          m_lastScope = 0;     // 上一次生效的范围档(警告拒绝时的回退目标)
    bool         m_updating = false;  // 程序化批量改勾选中(拦中间态 emit)
};
