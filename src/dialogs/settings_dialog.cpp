#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"
#include "i18n.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QMessageBox>
#include <QTableWidget>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QFrame>
#include <QTreeWidget>
#include <QListWidget>
#include <QPalette>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QMenu>
#include <QColorDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <functional>
#include "dbmaintenance.h"
#include "settings_dialog_internal.h"

using namespace sd_impl;   // pendingKeys / markPending / colorPick

// ── 页面构建助手 ──
// 带大标题+分隔线的页面包装(对标 XnView MP:右侧顶部粗体标题 + 白色分隔线,
// 深炭灰分层底色,不再纯黑)
QWidget* wrapTitled(const QString& title, QLayout* lay) {
    auto* page = new QWidget;
    page->setAutoFillBackground(true);
    QPalette pal = page->palette();
    pal.setColor(QPalette::Window, QColor(Theme::T("#1E1E1E", "#FFFFFF")));
    page->setPalette(pal);
    auto* v = new QVBoxLayout(page);
    // #148:全局紧凑(2026-09-01 用户令,此前 #108 只收紧异常空隔不够)
    v->setContentsMargins(14, 8, 14, 8);
    v->setSpacing(5);
    auto* h = new QLabel(title);
    h->setObjectName(QStringLiteral("settingsPageTitle"));
    // 标题钉死高度:否则页内剩余空间先喂给这个 Preferred 标题(探针实测被撑到
    // 519px,文字 AlignVCenter 浮在空带中央——即用户报的"标题上下间距过大")
    h->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    v->addWidget(h);
    auto* line = new QFrame;
    line->setFrameShape(QFrame::HLine);
    line->setFixedHeight(1);
    line->setObjectName(QStringLiteral("settingsPageRule"));
    v->addWidget(line);
    v->addLayout(lay, 1);
    // 组框全被 #156 钉 Fixed,内层 max 顶死 → stretch=1 失效,剩余空间会摊进各
    // cell(元素垂直居中、缝变大)。页尾兜底弹簧独占剩余;stretch=0 不与长页内容争空间
    v->addStretch(0);
    return page;
}

// 分组框(对齐 XnView:组名在框外上方,浅色细线圆角框,内部紧凑)
QWidget* group(const QString& title, QLayout* lay) {
    auto* w = new QWidget;
    // wrapTitled 给内容 stretch=1,组框默认 Preferred 会连标题带框被摊满整页高度
    // (框内大面积留白、行垂直居中) —— 垂直钉死:组框贴内容,多余空间留页尾
    w->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
    // 容器透明原是裸声明(泼给组内全部控件);应用级 QSS 里由
    // QWidget#settingsGroup 系列规则复刻(#89 收敛)
    w->setObjectName(QStringLiteral("settingsGroup"));
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(0, 0, 0, 0);
    v->setSpacing(2);   // 标题贴框:组标题与框之间的缝
    auto* t = new QLabel(title);
    t->setObjectName(QStringLiteral("settingsGroupTitle"));
    v->addWidget(t);
    auto* frame = new QFrame;
    frame->setObjectName(QStringLiteral("grpFrame"));
    auto* fl = new QVBoxLayout(frame);
    fl->setContentsMargins(10, 4, 10, 4);   // #148:框内再收一档
    fl->setSpacing(4);
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
        { "标题栏",     1, &SettingsDialog::pageTitlebar },
        { "切换模式",   1, &SettingsDialog::pageSwitchMode },
        { "键盘",       1, &SettingsDialog::pageKeyboardMouse },
        { "快捷键",     1, &SettingsDialog::pageShortcuts },
        { "浏览器",     0, &SettingsDialog::pageBrowser },
        { "文件列表",   1, &SettingsDialog::pageFileList },
        { "缩略图",     0, &SettingsDialog::pageThumbs },
        { "外观",       1, &SettingsDialog::pageAppearance },
        { "标签颜色",   1, &SettingsDialog::pageLabelColors },
        { "查看",       0, &SettingsDialog::pageViewer },
        { "其他",       1, &SettingsDialog::pageViewerOther },
        { "全屏",       1, &SettingsDialog::pageFullscreen },
        { "缓存数据库", 0, &SettingsDialog::pageCache },
        { "维护",       0, &SettingsDialog::pageMaintenance },
        { "系统集成",   0, &SettingsDialog::pageIntegration },
        { "以文搜图",   0, &SettingsDialog::pageImgSearch },
    };
    QTreeWidgetItem* parent = nullptr;
    for (const auto& nd : nodes) {
        // indentation=0(选中蓝条全宽),二级缩进由文本前缀空格模拟(加大层级差)
        QString label = gazeTr(nd.title);
        if (nd.level > 0) label.prepend(QString(8, ' '));
        auto* item = new QTreeWidgetItem(QStringList() << label);
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
        // #126:记住颜色编辑器所在页,外观页那颗按钮要跳进来
        if (nd.fn == &SettingsDialog::pageLabelColors) m_labelColorsItem = item;
    }
    m_cats->expandAll();
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(gazeTr("设置"));
    resize(900, 660);
    // 中文字体用雅黑渲染(默认字体小字发虚)
    setFont(QFont(QString::fromUtf8("Microsoft YaHei UI"), 9));
    // 整表样式(树/输入/下拉/数字框/按钮/组框…)在应用级 QSS
    // (QDialog#settingsDialog 规则组,#89 收敛),切主题即时跟上
    setObjectName(QStringLiteral("settingsDialog"));

    auto* root = new QVBoxLayout(this);          // 外层垂直:内容区 + 底部按钮行
    auto* content = new QHBoxLayout;             // 分类树 + 页面

    // 左列:过滤器搜索框 + 分类树
    auto* leftCol = new QVBoxLayout;
    leftCol->setContentsMargins(0, 0, 0, 0);
    leftCol->setSpacing(6);
    auto* filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(gazeTr("过滤器"));
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
    // (三钮样式在应用级 QSS:settingsStdBtn=工具条底+灰描边,okBtn=蓝底白字)
    auto* bottom = new QHBoxLayout;
    auto* resetBtn = new QPushButton(gazeTr("恢复默认"));
    resetBtn->setObjectName(QStringLiteral("settingsStdBtn"));
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        // 确认框把后果说透:这不止是"改回默认值",是整份配置文件清空
        //(收藏夹、最近文件、全部个性化项一起没),用户有权知道斤两再点 Yes
        if (QMessageBox::question(this, gazeTr("恢复默认"),
            gazeTr("将清空整份配置文件 Gaze.ini:\n"
                   "\n全部设置回到默认,收藏夹与最近的文件也会一并清空,且无法恢复。\n"
                   "确定继续吗?"))
            == QMessageBox::Yes) {
            AppSettings::instance().clearAll();
            populatePages();
        }
    });
    bottom->addWidget(resetBtn);
    bottom->addStretch();
    auto* okBtn = new QPushButton(gazeTr("确定"));
    okBtn->setObjectName("okBtn");
    // 显式默认:Enter=确定;对话框空格过滤器的候选也按 default 优先
    // (不设的话 autoDefault 会先命中创建更早的"恢复默认")
    okBtn->setDefault(true);
    auto* cancelBtn = new QPushButton(gazeTr("取消"));
    cancelBtn->setObjectName(QStringLiteral("settingsStdBtn"));
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
    markPending(c, key);
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
    markPending(c, key);
    return c;
}

QSpinBox* SettingsDialog::spin(const QString& key, int min, int max, int def) {
    auto* s = new QSpinBox;
    s->setRange(min, max);
    s->setValue(AppSettings::instance().get(key, def).toInt());
    connect(s, &QSpinBox::valueChanged, this, [key](int v) {
        AppSettings::instance().set(key, v);
    });
    markPending(s, key);
    return s;
}

QLineEdit* SettingsDialog::edit(const QString& key, const QString& def) {
    auto* e = new QLineEdit(AppSettings::instance().get(key, def).toString());
    connect(e, &QLineEdit::textChanged, this, [key](const QString& v) {
        AppSettings::instance().set(key, v);
    });
    markPending(e, key);
    return e;
}
