#include "settings_dialog.h"
#include "settings.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QScrollArea>
#include <QFrame>
#include <QTreeWidget>
#include <QPalette>
#include "settings_dialog_internal.h"

using namespace sd_impl;   // pendingKeys / markPending / colorPick

// ── 页面构建助手 ──
// 带大标题+分隔线的页面包装(对标 XnView MP:右侧顶部粗体标题 + 1px 分隔线)
QWidget* wrapTitled(const QString& title, QLayout* lay) {
    auto* page = new QWidget;
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, QColor("#000000"));
    page->setPalette(pal);
    auto* v = new QVBoxLayout(page);
    v->setContentsMargins(26, 22, 26, 18);
    v->setSpacing(14);
    auto* h = new QLabel(title);
    h->setStyleSheet("font-size:19px;font-weight:600;color:#FFFFFF;"
                     "background:transparent;");
    v->addWidget(h);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setStyleSheet("background:#3A3A42;border:none;");
    v->addWidget(line);
    v->addLayout(lay, 1);
    return page;
}

// 分组框(对齐 XnView:组名在框外上方,浅色细线圆角框,内部紧凑)
QWidget* group(const QString& title, QLayout* lay) {
    auto* w = new QWidget;
    w->setStyleSheet("background:transparent;");
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);   // 标题贴框:组标题与框之间的缝
    auto* t = new QLabel(title);
    t->setStyleSheet("font-size:12px;font-weight:600;color:#E8E8E8;"
                     "background:transparent;");
    v->addWidget(t);
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("grpFrame"));
    frame->setStyleSheet(
        "QFrame#grpFrame{background:" C_CARD_BG ";border:1px solid " C_SEPARATOR ";border-radius:5px;}");
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(12, 6, 12, 6);
    fl->setSpacing(5);
    fl->addLayout(lay);
    v->addWidget(frame);
    return w;
}

void SettingsDialog::populatePages() {
    m_cats->clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }
    // 两级分类树:level 0=一级(加粗,自身也是页面可点进) 1=二级子项
    struct Node { const char* title; int level; QWidget* (SettingsDialog::*fn)(); };
    static const Node nodes[] = {
        { "常规",       0, &SettingsDialog::pageGeneral },
        { "启动",       1, &SettingsDialog::pageStartup },
        { "文件操作",   1, &SettingsDialog::pageFileOps },
        { "界面",       1, &SettingsDialog::pageInterface },
        { "切换模式",   1, &SettingsDialog::pageSwitchMode },
        { "键盘和鼠标", 1, &SettingsDialog::pageKeyboardMouse },
        { "快捷键",     1, &SettingsDialog::pageShortcuts },
        { "浏览器",     0, &SettingsDialog::pageBrowser },
        { "文件列表",   1, &SettingsDialog::pageFileList },
        { "缩略图",     0, &SettingsDialog::pageThumbs },
        { "外观",       1, &SettingsDialog::pageAppearance },
        { "查看",       0, &SettingsDialog::pageViewer },
        { "全屏",       1, &SettingsDialog::pageFullscreen },
        { "缓存数据库", 0, &SettingsDialog::pageCache },
        { "系统集成",   0, &SettingsDialog::pageIntegration },
    };
    QTreeWidgetItem* parent = nullptr;
    for (const auto& nd : nodes) {
        auto* item = new QTreeWidgetItem(
            QStringList() << QString::fromUtf8(nd.title));
        if (nd.level == 0) {
            m_cats->addTopLevelItem(item);
            parent = item;
            QFont f = item->font(0);
            f.setBold(true);
            item->setFont(0, f);
        } else {
            parent->addChild(item);
        }
        // 每页包一层无边框滚动区(内容超高可滚,不溢出)
        auto* sc = new QScrollArea;
        sc->setWidgetResizable(true);
        sc->setFrameShape(QFrame::NoFrame);
        sc->setWidget((this->*nd.fn)());
        m_stack->addWidget(sc);
        item->setData(0, Qt::UserRole, m_stack->count() - 1);
    }
    m_cats->expandAll();
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("设置"));
    resize(900, 660);
    // 深炭灰分层配色(对齐 XnView MP:窗体 #1E1E1E,左列表更暗,选中=全宽蓝条)
    setStyleSheet(
        "QDialog{background:" C_WIN_BG ";}"
        "QTreeWidget{background:" C_CONTENT ";color:#FFFFFF;border:none;outline:none;}"
        "QTreeWidget::item{height:30px;padding:0 8px;border:none;}"
        "QTreeWidget::item:hover{background:" C_CARD_HOVER ";border:none;}"
        "QTreeWidget::item:selected{background:" C_ACCENT ";color:#FFFFFF;border:none;}"
        "QLineEdit{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:4px 8px;}"
        "QLineEdit[readOnly=\"true\"]{color:#B8B8B8;background:" C_CONTENT ";}"
        "QLabel{color:#FFFFFF;background:transparent;}"
        "QCheckBox{color:#FFFFFF;background:transparent;spacing:6px;}"
        "QComboBox{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:3px 8px;min-width:180px;}"
        "QComboBox QAbstractItemView{background:" C_CONTENT ";color:#FFFFFF;"
        "selection-background-color:" C_ACCENT ";}"
        "QSpinBox{background:" C_CONTENT ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:3px 6px;}"
        "QPushButton{background:" C_TOOLBAR ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:5px 16px;}"
        "QPushButton:hover{border-color:" C_ACCENT ";}"
        "QGroupBox{color:#E8E8E8;font-weight:600;border:1px solid " C_SEPARATOR ";"
        "border-radius:5px;margin-top:12px;padding:14px 10px 10px 10px;"
        "background:" C_WIN_BG ";}"
        "QGroupBox::title{subcontrol-origin:margin;left:12px;top:2px;}"
        "QScrollArea{background:" C_WIN_BG ";border:none;}"
        "QToolButton{background:" C_TOOLBAR ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "border-radius:3px;padding:3px 10px;}"
        "QToolButton:hover{border-color:" C_ACCENT ";}");

    auto* root = new QVBoxLayout(this);          // 外层垂直:内容区 + 底部按钮行
    auto* content = new QHBoxLayout;             // 分类树 + 页面

    // 左列:过滤器搜索框 + 分类树
    auto* leftCol = new QVBoxLayout;
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(6);
    auto* filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(QString::fromUtf8("过滤器"));
    filterEdit->setFixedWidth(180);
    filterEdit->setClearButtonEnabled(true);
    leftCol->addWidget(filterEdit);

    m_cats = new QTreeWidget;
    m_cats->setColumnCount(1);
    m_cats->setHeaderHidden(true);
    m_cats->setRootIsDecorated(false);           // 无展开箭头
    m_cats->setIndentation(0);                   // 缩进由文本前缀模拟 → 选中=全宽蓝条
    m_cats->setUniformRowHeights(true);
    m_cats->setAnimated(true);                   // 展开/收起动画
    m_cats->setFixedWidth(180);
    leftCol->addWidget(m_cats, 1);
    leftCol->addStretch(0);

    m_stack = new QStackedWidget;

    content->addLayout(leftCol);
    content->addWidget(m_stack, 1);
    root->addLayout(content, 1);

    populatePages();
    connect(m_cats, &QTreeWidget::currentItemChanged, this,
            [this](QTreeWidgetItem* cur, QTreeWidgetItem*) {
                if (!cur) return;
                m_stack->setCurrentIndex(cur->data(0, Qt::UserRole).toInt());
            });
    m_cats->setCurrentItem(m_cats->topLevelItem(0));

    // 过滤器:命中项+其祖先链显示,其余隐藏;清空恢复全部
    connect(filterEdit, &QLineEdit::textChanged, this, [this](const QString& text) {
        const QString t = text.trimmed();
        std::function<bool(QTreeWidgetItem*)> walk = [&](QTreeWidgetItem* p) -> bool {
            bool self = p->text(0).trimmed().contains(t, Qt::CaseInsensitive);
            bool childHit = false;
            for (int i = 0; i < p->childCount(); ++i)
                childHit |= walk(p->child(i));
            bool show = t.isEmpty() || self || childHit;
            p->setHidden(!show);
            return self || childHit;
        };
        for (int i = 0; i < m_cats->topLevelItemCount(); ++i)
            walk(m_cats->topLevelItem(i));
        if (t.isEmpty()) m_cats->expandAll();
    });

    // 底部按钮行:左"恢复默认",右下角 确定 / 取消
    auto* bottom = new QHBoxLayout;
    const char* btnQss =
        "QPushButton{background:" C_TOOLBAR ";color:#FFFFFF;border:1px solid " C_SEPARATOR ";"
        "padding:6px 28px;border-radius:4px;}"
        "QPushButton:hover{border-color:" C_ACCENT ";}"
        "QPushButton#okBtn{background:" C_ACCENT ";border-color:" C_ACCENT ";color:#FFF;}"
        "QPushButton#okBtn:hover{background:" C_ACCENT_DOWN ";}";
    auto* resetBtn = new QPushButton(QString::fromUtf8("恢复默认"));
    resetBtn->setStyleSheet(btnQss);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QString::fromUtf8("恢复默认"),
            QString::fromUtf8("将所有设置恢复为默认值?(gaze.ini 将被清空)"))
            == QMessageBox::Yes) {
            AppSettings::instance().clearAll();
            populatePages();
        }
    });
    bottom->addWidget(resetBtn);
    bottom->addStretch();
    auto* okBtn = new QPushButton(QString::fromUtf8("确定"));
    okBtn->setObjectName("okBtn");
    okBtn->setStyleSheet(btnQss);
    auto* cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    cancelBtn->setStyleSheet(btnQss);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(okBtn);
    bottom->addWidget(cancelBtn);
    root->addLayout(bottom);
}

// ═══ 控件工厂:载入当前值,变更即时保存 ═══
QCheckBox* SettingsDialog::chk(const QString& key, const QString& label, bool def) {
    auto* c = new QCheckBox(label);
    c->setChecked(AppSettings::instance().get(key, def).toBool());
    connect(c, &QCheckBox::toggled, this, [key](bool v) {
        AppSettings::instance().set(key, v);
    });
    return c;
}

QComboBox* SettingsDialog::combo(const QString& key, const QStringList& items, int defIdx) {
    auto* c = new QComboBox;
    c->addItems(items);
    int cur = AppSettings::instance().get(key, defIdx).toInt();
    c->setCurrentIndex(qBound(0, cur, items.size() - 1));
    connect(c, &QComboBox::currentIndexChanged, this, [key](int v) {
        AppSettings::instance().set(key, v);
    });
    return c;
}

QSpinBox* SettingsDialog::spin(const QString& key, int min, int max, int def) {
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(AppSettings::instance().get(key, def).toInt());
    connect(s, &QSpinBox::valueChanged, this, [key](int v) {
        AppSettings::instance().set(key, v);
    });
    return s;
}

QLineEdit* SettingsDialog::edit(const QString& key, const QString& def) {
    auto* e = new QLineEdit(AppSettings::instance().get(key, def).toString());
    connect(e, &QLineEdit::textChanged, this, [key](const QString& v) {
        AppSettings::instance().set(key, v);
    });
    return e;
}
