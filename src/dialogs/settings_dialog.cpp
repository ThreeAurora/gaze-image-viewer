#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"
#include "viewerhotkeys.h"

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

void SettingsDialog::populatePages() {
    m_cats->clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }
    struct { const char* name; QWidget* (SettingsDialog::*fn)(); } pages[] = {
        { "常规",     &SettingsDialog::pageGeneral },
        { "启动",     &SettingsDialog::pageStartup },
        { "文件操作", &SettingsDialog::pageFileOps },
        { "界面",     &SettingsDialog::pageInterface },
        { "键盘和鼠标", &SettingsDialog::pageKeyboardMouse },
        { "快捷键",     &SettingsDialog::pageShortcuts },
        { "切换模式", &SettingsDialog::pageSwitchMode },
        { "浏览器",   &SettingsDialog::pageBrowser },
        { "文件列表", &SettingsDialog::pageFileList },
        { "缩略图",   &SettingsDialog::pageThumbs },
        { "外观",     &SettingsDialog::pageAppearance },
        { "查看",     &SettingsDialog::pageViewer },
        { "全屏",     &SettingsDialog::pageFullscreen },
        { "缓存数据库", &SettingsDialog::pageCache },
        { "系统集成", &SettingsDialog::pageIntegration },
    };
    for (auto& p : pages) {
        m_cats->addItem(QString::fromUtf8(p.name));
        m_stack->addWidget((this->*p.fn)());
    }
}

SettingsDialog::SettingsDialog(QWidget* parent) : QDialog(parent) {
    setWindowTitle(QString::fromUtf8("设置"));
    resize(860, 620);
    setStyleSheet(
        "QDialog{background:#1B1B1F;}"
        "QListWidget{background:#17171A;color:#E0E0E4;border:1px solid #303036;}"
        "QListWidget::item{padding:6px 14px;}"
        "QListWidget::item:selected{background:#3B82F6;color:#FFF;}"
        "QLabel{color:#E0E0E4;background:transparent;}"
        "QCheckBox{color:#E0E0E4;background:transparent;spacing:6px;}"
        "QComboBox{background:#232328;color:#E0E0E4;border:1px solid #303036;"
        "border-radius:4px;padding:3px 8px;min-width:180px;}"
        "QSpinBox{background:#232328;color:#E0E0E4;border:1px solid #303036;"
        "border-radius:4px;padding:3px 6px;}"
        "QLineEdit{background:#232328;color:#E0E0E4;border:1px solid #303036;"
        "border-radius:4px;padding:3px 8px;}"
        "QGroupBox{color:#9C9CA4;border:1px solid #303036;border-radius:6px;"
        "margin-top:10px;padding-top:6px;}"
        "QGroupBox::title{subcontrol-origin:margin;left:10px;}");

    auto* root = new QVBoxLayout(this);          // 外层垂直:内容区 + 底部按钮行
    auto* content = new QHBoxLayout;             // 分类 + 页面

    m_cats = new QListWidget;
    m_cats->setFixedWidth(170);

    m_stack = new QStackedWidget;

    content->addWidget(m_cats);
    content->addWidget(m_stack, 1);
    root->addLayout(content, 1);

    populatePages();
    connect(m_cats, &QListWidget::currentRowChanged,
            m_stack, &QStackedWidget::setCurrentIndex);
    m_cats->setCurrentRow(0);

    // 底部按钮行:左"恢复默认",右下角 确定 / 取消
    auto* bottom = new QHBoxLayout;
    const char* btnQss =
        "QPushButton{background:#2C2C32;color:#E0E0E4;border:1px solid #3A3A42;"
        "padding:6px 28px;border-radius:4px;}"
        "QPushButton:hover{border-color:#3B82F6;}"
        "QPushButton#okBtn{background:#3B82F6;border-color:#3B82F6;color:#FFF;}"
        "QPushButton#okBtn:hover{background:#2F6FE0;}";
    auto* resetBtn = new QPushButton(QString::fromUtf8("恢复默认"));
    resetBtn->setStyleSheet(btnQss);
    connect(resetBtn, &QPushButton::clicked, this, [this]() {
        if (QMessageBox::question(this, QString::fromUtf8("恢复默认"),
            QString::fromUtf8("将所有设置恢复为默认值?"))
            == QMessageBox::Yes) {
            AppSettings::instance().clearAll();
            populatePages();
        }
    });
    bottom->addWidget(resetBtn);
    bottom->addStretch();
    auto* okBtn = new QPushButton(QString::fromUtf8("确定"));
    okBtn->setObjectName("okBtn");
    // 显式默认:Enter=确定;对话框空格过滤器的候选也按 default 优先
    // (不设的话 autoDefault 会先命中创建更早的"恢复默认")
    okBtn->setDefault(true);
    okBtn->setStyleSheet(btnQss);
    auto* cancelBtn = new QPushButton(QString::fromUtf8("取消"));
    cancelBtn->setStyleSheet(btnQss);
    connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    bottom->addWidget(okBtn);
    bottom->addWidget(cancelBtn);
    root->addLayout(bottom);
}

void SettingsDialog::populatePages() {
    m_cats->clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }
    struct { const char* name; QWidget* (SettingsDialog::*fn)(); } pages[] = {
        { "常规",     &SettingsDialog::pageGeneral },
        { "启动",     &SettingsDialog::pageStartup },
        { "文件操作", &SettingsDialog::pageFileOps },
        { "界面",     &SettingsDialog::pageInterface },
        { "键盘和鼠标", &SettingsDialog::pageKeyboardMouse },
        { "切换模式", &SettingsDialog::pageSwitchMode },
        { "浏览器",   &SettingsDialog::pageBrowser },
        { "文件列表", &SettingsDialog::pageFileList },
        { "缩略图",   &SettingsDialog::pageThumbs },
        { "外观",     &SettingsDialog::pageAppearance },
        { "查看",     &SettingsDialog::pageViewer },
        { "全屏",     &SettingsDialog::pageFullscreen },
        { "缓存数据库", &SettingsDialog::pageCache },
        { "系统集成", &SettingsDialog::pageIntegration },
    };
    for (auto& p : pages) {
        m_cats->addItem(QString::fromUtf8(p.name));
        m_stack->addWidget((this->*p.fn)());
    }
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
    QObject::connect(e, &QLineEdit::textChanged, [key](const QString& v) {
        AppSettings::instance().set(key, v);
    });
    return e;
}

// ═══ 页面 ═══
static QWidget* wrapPage(QLayout* lay) {
    auto* w = new QWidget;
    auto* v = new QVBoxLayout(w);
    v->addLayout(lay);
    v->addStretch();
    v->setContentsMargins(16, 16, 16, 16);
    return w;
}

QWidget* SettingsDialog::pageGeneral() {
    auto* form = new QFormLayout;
    form->addRow(chk("General/singleInstance", QString::fromUtf8("仅允许运行一个 xnnview 程序实例"), false));
    form->addRow(chk("General/exifRotate", QString::fromUtf8("根据 EXIF 方向标签旋转图像"), true));
    form->addRow(chk("General/exifDpi", QString::fromUtf8("使用 EXIF DPI(如果存在)"), true));
    form->addRow(chk("General/dpiAdjust", QString::fromUtf8("若 X/Y DPI 不相等,调整缩放"), true));
    // 默认"始终":与接线前的既有行为一致(退出即记住目录与选中文件)
    form->addRow(QString::fromUtf8("退出程序时保存会话"),
        combo("General/saveSession", {QString::fromUtf8("从不"), QString::fromUtf8("询问"), QString::fromUtf8("始终")}, 2));
    return wrapPage(form);
}

QWidget* SettingsDialog::pageStartup() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(QString::fromUtf8("带文件启动"),
        combo("Start/withFile", {QString::fromUtf8("查看器"), QString::fromUtf8("全屏 - 查看器"),
                                 QString::fromUtf8("浏览器"), QString::fromUtf8("浏览器 - 全屏")}, 0));
    form->addRow(QString::fromUtf8("不带文件启动"),
        combo("Start/withoutFile", {QString::fromUtf8("无"), QString::fromUtf8("上次使用的目录"),
                                    QString::fromUtf8("指定目录")}, 1));
    form->addRow(chk("Start/rememberFilename", QString::fromUtf8("记录选择的文件名"), true));
    return wrapTitled(QString::fromUtf8("启动"), form);
}

QWidget* SettingsDialog::pageFileOps() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(chk("FileOps/confirmDelete", QString::fromUtf8("文件删除前确认"), true));
    form->addRow(chk("FileOps/confirmDeleteDirs",
        QString::fromUtf8("删除含文件夹时确认(不受上一项影响)"), true));
    form->addRow(chk("FileOps/useRecycleBin",
        QString::fromUtf8("使用回收站(关闭后永久删除,并强制确认)"), true));
    form->addRow(chk("FileOps/deleteToast",
        QString::fromUtf8("删除后在左下角显示提示"), true));
    auto* toastMs = spin("FileOps/toastMs", 400, 8000, 1500);
    toastMs->setSingleStep(100);
    toastMs->setSuffix(QString::fromUtf8(" 毫秒"));
    form->addRow(QString::fromUtf8("删除提示停留时长"), toastMs);
    form->addRow(chk("FileOps/losslessBackup", QString::fromUtf8("为无损翻转/旋转生成备份"), true));
    form->addRow(chk("FileOps/losslessKeepMeta", QString::fromUtf8("为无损翻转/旋转保留原始元数据"), true));
    form->addRow(chk("FileOps/renameDialog", QString::fromUtf8("使用对话框重命名文件/文件夹"), true));
    form->addRow(QString::fromUtf8("重复文件命名"),
        combo("FileOps/duplicateTemplate",
              {"<文件名>-(#)", "<文件名> - 副本 (#)", "<文件名>-副本 (#)", "<文件名>-#", "副本 (#) - <文件名>"}, 0));
    form->addRow(new QLabel(QString::fromUtf8(
        "说明:右键无损旋转/翻转会先复制原件再旋转,不修改任何元数据(含创建/修改时间)。")));
    return wrapTitled(QString::fromUtf8("文件操作"), form);
}

QWidget* SettingsDialog::pageInterface() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    auto* multiTabs = chk("Interface/multiViewerTabs",
                          QString::fromUtf8("同一文件多个查看器标签卡"), false);
    multiTabs->setToolTip(QString::fromUtf8(
        "开:同一个文件可以再开一张标签(右键\"在新标签卡中打开\"点两次就有两张)。\n"
        "关(默认):一个文件只占一张标签,重复打开就切回已有那张。"));
    form->addRow(multiTabs);
    auto* syncBrowser = chk("Interface/syncBrowser",
                            QString::fromUtf8("关闭视图时,同步调整浏览器"), false);
    syncBrowser->setToolTip(QString::fromUtf8(
        "开:切换查看器标签、以及退回浏览器时,把文件列表的选中项挪到那个文件上,\n"
        "于是退回后高亮的就是刚才最后看的那张,标题栏与预览也都跟着它。\n"
        "关(默认):退回浏览器后列表仍停在你进查看器前的那一行。"));
    form->addRow(syncBrowser);
    auto* oneTab = chk("Interface/oneViewerTab",
                       QString::fromUtf8("一个文件仅有一个查看器标签卡"), false);
    oneTab->setToolTip(QString::fromUtf8(
        "开:查看器始终只保留一张标签,新打开的文件顶掉当前标签。\n"
        "关(默认):右键\"在新标签卡中打开\"每张另起一条,张数受下面的上限约束。"));
    form->addRow(oneTab);
    form->addRow(QString::fromUtf8("查看器标签卡上限(0=不限)"),
        spin("Interface/maxViewerTabs", 0, 100, 20));
    auto* panesOnStart = chk("Interface/showPanesOnStart",
        QString::fromUtf8("启动时打开文件列表和预览框"), true);
    panesOnStart->setToolTip(QString::fromUtf8(
        "勾选:启动时强制显示文件夹树与预览面板。\n"
        "不勾选:沿用上次退出时的面板开关(需保存布局或启用\"应用关闭时的布局\")。"));
    form->addRow(panesOnStart);
    auto* panesOnStart = chk("Interface/showPanesOnStart",
        QString::fromUtf8("启动时打开文件列表和预览框"), true);
    panesOnStart->setToolTip(QString::fromUtf8(
        "勾选:启动时强制显示文件夹树与预览面板。\n"
        "不勾选:沿用上次退出时的面板开关(需保存布局或启用\"应用关闭时的布局\")。"));
    form->addRow(panesOnStart);
    auto* panesOnStart = chk("Interface/showPanesOnStart",
        QString::fromUtf8("启动时打开文件列表和预览框"), true);
    panesOnStart->setToolTip(QString::fromUtf8(
        "勾选:启动时强制显示文件夹树与预览面板。\n"
        "不勾选:沿用上次退出时的面板开关(需保存布局或启用\"应用关闭时的布局\")。"));
    form->addRow(panesOnStart);
    auto* panesOnStart = chk("Interface/showPanesOnStart",
        QString::fromUtf8("启动时打开文件列表和预览框"), true);
    panesOnStart->setToolTip(QString::fromUtf8(
        "勾选:启动时强制显示文件夹树与预览面板。\n"
        "不勾选:沿用上次退出时的面板开关(需保存布局或启用\"应用关闭时的布局\")。"));
    form->addRow(panesOnStart);
    form->addRow(QString::fromUtf8("最近的文件上限数量(0-100)"),
        spin("Interface/maxRecent", 0, 100, 20));
    form->addRow(chk("Interface/clearRecentOnExit", QString::fromUtf8("退出时清理\"最近的文件\"记录"), false));
    return wrapTitled(QString::fromUtf8("界面"), form);
}

// ── 标题栏页:浏览器模式/查看器两组,每组模板输入框+▶ 变量菜单 ──
static QWidget* titleTemplateRow(const QString& key, const QString& def);

QWidget* SettingsDialog::pageTitlebar() {
    auto* root = new QVBoxLayout;
    root->setSpacing(12);

    auto* fBr = new QFormLayout;
    fBr->setVerticalSpacing(10);
    fBr->addRow(titleTemplateRow("Interface/titleBrowser",
                                 QString::fromUtf8("{文件夹} - {文件名 含扩展名} - Gaze")));
    root->addWidget(group(QString::fromUtf8("浏览器模式"), fBr));

    auto* fVw = new QFormLayout;
    fVw->setVerticalSpacing(10);
    fVw->addRow(titleTemplateRow("Interface/titleViewer",
                                 QString::fromUtf8("{文件夹} - {文件名 含扩展名} - Gaze")));
    root->addWidget(group(QString::fromUtf8("查看器"), fVw));
    return wrapTitled(QString::fromUtf8("标题栏"), root);
}

// 标题栏模板编辑行:输入框 + ▶ 变量插入菜单(点击变量插入光标处,对齐 XnView 交互)
static QWidget* titleTemplateRow(const QString& key, const QString& def) {
    auto* row = new QWidget;
    auto* h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(6);
    auto* e = new QLineEdit(AppSettings::instance().get(key, def).toString());
    QObject::connect(e, &QLineEdit::textChanged, [key](const QString& v) {
        AppSettings::instance().set(key, v);
    });
    h->addWidget(e, 1);
    auto* btn = new QToolButton;
    btn->setText(QString::fromUtf8("▶"));
    btn->setToolTip(QString::fromUtf8("插入变量"));
    h->addWidget(btn);

    auto* menu = new QMenu(btn);
    auto addVar = [menu, e](const QString& var) {
        menu->addAction(var, e, [e, var]() { e->insert("{" + var + "}"); });
    };
    addVar(QString::fromUtf8("文件名"));
    addVar(QString::fromUtf8("文件名 含扩展名"));
    addVar(QString::fromUtf8("文件夹"));
    addVar(QString::fromUtf8("文件夹名"));
    addVar(QString::fromUtf8("路径"));
    addVar(QString::fromUtf8("大小"));
    addVar(QString::fromUtf8("创建日期"));
    addVar(QString::fromUtf8("修改日期"));
    addVar(QString::fromUtf8("宽"));
    addVar(QString::fromUtf8("高"));
    addVar(QString::fromUtf8("颜色标签"));
    auto* act = menu->addAction(QString::fromUtf8("评级"));
    act->setEnabled(false);
    act->setText(QString::fromUtf8("评级(程序无评级模型,恒为空)"));
    auto* timeMenu = menu->addMenu(QString::fromUtf8("时间格式变量"));
    auto addTime = [timeMenu, e](const QString& var) {
        timeMenu->addAction(var, e, [e, var]() { e->insert("{" + var + "}"); });
    };
    auto addHeader = [timeMenu](const QString& text) {
        timeMenu->addAction(text)->setEnabled(false);
    };
    // 大写=修改时间,小写=创建时间;N/n=分钟(与 M/m=月 区分)
    addHeader(QString::fromUtf8("修改时间 年/月/日 时/分/秒"));
    for (const char* v : { "Y", "M", "D", "H", "N", "S" }) addTime(v);
    addHeader(QString::fromUtf8("创建时间 年/月/日 时/分/秒"));
    for (const char* v : { "y", "m", "d", "h", "n", "s" }) addTime(v);
    timeMenu->addSeparator();
    for (const char* v : { "Y-m-d_H-N-S", "Y_m_d_H_N_S",
                           "y-m-d_h-n-s", "y_m_d_h_n_s" }) addTime(v);
    QObject::connect(btn, &QToolButton::clicked, btn, [btn, menu]() {
        menu->exec(btn->mapToGlobal(QPoint(0, btn->height())));
    });
    return row;
}

QWidget* SettingsDialog::pageKeyboardMouse() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(QString::fromUtf8("左/右键方向键"),
        combo("Keyboard/leftRight", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("水平滚动")}, 0));
    form->addRow(QString::fromUtf8("上/下方向键"),
        combo("Keyboard/upDown", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("向上/向下翻页")}, 0));
    form->addRow(QString::fromUtf8("空格"),
        combo("Keyboard/space", {QString::fromUtf8("播放/暂停(视频)"),
            QString::fromUtf8("什么都不做"),
            QString::fromUtf8("下一个文件"), QString::fromUtf8("快速幻灯片")}, 0));
    form->addRow(QString::fromUtf8("快速幻灯片间隔(毫秒)"),
        spin("Interface/slideInterval", SLIDE_MS_MIN, SLIDE_MS_MAX, SLIDE_MS_DEF));
    form->addRow(chk("Keyboard/escCloseBrowser", QString::fromUtf8("按 ESC 关闭:浏览器模式"), false));
    form->addRow(chk("Keyboard/escCloseViewer", QString::fromUtf8("按 ESC 关闭:查看器"), true));
    return wrapTitled(QString::fromUtf8("键盘"), form);
}

QWidget* SettingsDialog::pageShortcuts() {
    auto* v = new QVBoxLayout;
    v->addWidget(new QLabel(QString::fromUtf8(
        "点击快捷键框后按下新组合键即可修改(按 Esc/Backspace 清除恢复默认)。变更即时保存并生效。")));

    auto* table = new QTableWidget(0, 2);
    table->setHorizontalHeaderLabels({QString::fromUtf8("功能"),
                                      QString::fromUtf8("快捷键")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setStyleSheet(
        "QTableWidget{background:#17171A;color:#FFFFFF;border:1px solid #303036;}"
        "QHeaderView::section{background:#232328;color:#FFFFFF;border:none;padding:4px;}");
    v->addWidget(table, 1);

    // 遍历主窗口菜单里所有带快捷键的功能
    if (QWidget* mw = parentWidget()) {
        QList<QAction*> acts = mw->findChildren<QAction*>();
        for (QAction* a : acts) {
            if (a->shortcut().isEmpty() || a->text().isEmpty()
                || a->menu() != nullptr || a->isSeparator())
                continue;
            int r = table->rowCount();
            table->insertRow(r);
            QString name = a->text();
            name.remove('&');
            table->setItem(r, 0, new QTableWidgetItem(name));
            auto* ed = new QKeySequenceEdit(a->shortcut());
            QString key = QString("Shortcuts/") + a->text().remove('&');
            connect(ed, &QKeySequenceEdit::keySequenceChanged, this,
                    [a, key](const QKeySequence& ks) {
                        AppSettings::instance().set(key, ks.toString());
                        a->setShortcut(ks);
                    });
            table->setCellWidget(r, 1, ed);
        }
    }

    // 表格撑满页面高度
    v->addWidget(table, 1);
    return wrapTitled(QString::fromUtf8("快捷键"), v);
}

QWidget* SettingsDialog::pageSwitchMode() {
    QStringList opts = QStringList{
        QString::fromUtf8("浏览器 ↔ 全屏 | 查看器 ↔ 全屏"),
        QString::fromUtf8("浏览器 ↔ 查看器"),
        QString::fromUtf8("浏览器 → 全屏 → 查看器"),
        QString::fromUtf8("浏览器 → 查看器 → 全屏"),
        QString::fromUtf8("什么都不做"),
        QString::fromUtf8("用系统程序打开")};
    auto* form = new QFormLayout;
    // 双击默认"浏览器↔查看器"(#102)。非图像/非视频(压缩包、文档、exe…不受
    // 这项管)一律由系统默认程序打开 —— 那种文件没有"模式"可切。
    form->addRow(QString::fromUtf8("双击"),
        combo("SwitchMode/doubleClick", opts, 1));
    form->addRow(QString::fromUtf8("中键"),
        combo("SwitchMode/middleClick", opts, 4));
    form->addRow(QString::fromUtf8("回车键"),
        combo("SwitchMode/enterKey", opts, 1));
    return wrapTitled(QString::fromUtf8("切换模式"), form);
}

QWidget* SettingsDialog::pageBrowser() {
    auto* root = new QVBoxLayout;
    root->setSpacing(12);

    // 分组"预览"(对齐 XnView MP 浏览器页)
    auto* fPrev = new QFormLayout;
    fPrev->setVerticalSpacing(10);
    fPrev->addRow(QString::fromUtf8("预览背景色"),
                  colorPick("Browser/previewBackColor", "#000000"));
    fPrev->addRow(chk("Browser/showRating", QString::fromUtf8("显示评级(颜色标签)"), true));
    fPrev->addRow(chk("Preview/previewTxt", QString::fromUtf8("预览 txt 文本文件内容"), true));
    root->addWidget(group(QString::fromUtf8("预览"), fPrev));

    // 分组"旋转"
    auto* fRot = new QFormLayout;
    fRot->setVerticalSpacing(10);
    fRot->addRow(chk("Browser/rotateExifOnly", QString::fromUtf8("仅改变 EXIF 方向(如果可能)"), true));
    fRot->addRow(chk("Browser/losslessRotate", QString::fromUtf8("使用无损旋转(如果可能)"), true));
    root->addWidget(group(QString::fromUtf8("旋转"), fRot));

    auto* fMisc = new QFormLayout;
    fMisc->setVerticalSpacing(10);
    fMisc->addRow(chk("Browser/thumbScrollPreview", QString::fromUtf8("用缩略图查看滚动内容"), true));
    fMisc->addRow(chk("Browser/showDesktopInTree", QString::fromUtf8("在文件夹树中显示\"桌面\""), true));
    root->addLayout(fMisc);
    return wrapTitled(QString::fromUtf8("浏览器"), root);
}

QWidget* SettingsDialog::pageFileList() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(chk("FileList/showHidden", QString::fromUtf8("显示隐藏的文件和文件夹"), true));
    form->addRow(chk("FileList/recognizeByExt", QString::fromUtf8("只按扩展名进行识别文件格式"), true));
    form->addRow(QString::fromUtf8("扫描文件头"),
        combo("FileList/scanHeader", {QString::fromUtf8("总是"),
            QString::fromUtf8("排除软盘/CD/DVD"), QString::fromUtf8("仅电脑本地硬盘"),
            QString::fromUtf8("从不")}, 0));
    form->addRow(chk("FileList/mixSort", QString::fromUtf8("混合文件/文件夹排序"), false));
    form->addRow(chk("FileList/folderAlphabetical", QString::fromUtf8("文件夹总是按字母序排列"), true));
    form->addRow(chk("FileList/newAtEnd", QString::fromUtf8("新文件添加至列表末尾"), false));
    form->addRow(chk("FileList/autoSelectNew", QString::fromUtf8("自动选择新文件"), false));
    form->addRow(chk("FileList/sizeInBytes", QString::fromUtf8("按字节显示文件大小"), false));
    return wrapTitled(QString::fromUtf8("文件列表"), form);
}

QWidget* SettingsDialog::pageThumbs() {
    auto* root = new QVBoxLayout;
    root->setSpacing(12);

    // 分组"创建"
    auto* fCreate = new QFormLayout;
    fCreate->setVerticalSpacing(10);
    fCreate->addRow(chk("Thumbs/folder4", QString::fromUtf8("在文件夹缩略图中显示4张缩略图(而非1张)"), true));
    fCreate->addRow(chk("Thumbs/video4", QString::fromUtf8("在视频缩略图中显示4张缩略图(替代1张)"), false));
    fCreate->addRow(chk("Thumbs/highQuality", QString::fromUtf8("创建高品质的缩略图"), true));
    fCreate->addRow(chk("Thumbs/useEmbedded", QString::fromUtf8("使用嵌入缩略图"), true));
    fCreate->addRow(chk("Thumbs/embedFallback", QString::fromUtf8("当内嵌缩略图尺寸小于缩略图尺寸时从原图创建"), true));
    fCreate->addRow(chk("Thumbs/wholeFolder", QString::fromUtf8("为整个文件夹创建缩略图"), false));
    fCreate->addRow(QString::fromUtf8("视频提取帧位置(%,0=第 1 秒)"),
        spin("Thumbs/videoFramePct", 0, 100, 0));
    root->addWidget(group(QString::fromUtf8("创建"), fCreate));

    // 分组"处理"
    auto* fProc = new QFormLayout;
    fProc->setVerticalSpacing(10);
    fProc->addRow(chk("Thumbs/alpha", QString::fromUtf8("使用alpha通道"), true));
    fProc->addRow(chk("Thumbs/transparencyGrid", QString::fromUtf8("使用透明网格"), true));
    fProc->addRow(chk("Thumbs/sharpen", QString::fromUtf8("锐化缩略图"), false));
    fProc->addRow(chk("Thumbs/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    root->addWidget(group(QString::fromUtf8("处理"), fProc));
    return wrapTitled(QString::fromUtf8("缩略图"), root);
}

QWidget* SettingsDialog::pageAppearance() {
    auto* form = new QFormLayout;
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 宽"),
        spin("Appearance/customThumbW", THUMB_W_MIN, THUMB_W_MAX, 96));
    // 0 = 与宽同高(接线前的既有行为);>0 才按设置值固定缩略图框高
    auto* thumbH = spin("Appearance/customThumbH", 0, 512, 0);
    thumbH->setSpecialValueText(QString::fromUtf8("跟随宽度"));
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 高"), thumbH);
    form->addRow(chk("Appearance/shadow", QString::fromUtf8("使用阴影"), false));
    form->addRow(QString::fromUtf8("边框粗细"), spin("Appearance/borderSize", 0, 10, 0));
    form->addRow(QString::fromUtf8("间距"), spin("Appearance/spacing", 0, 40, 6));
    form->addRow(chk("Appearance/labelSpacing", QString::fromUtf8("标签间的间距"), true));
    form->addRow(QString::fromUtf8("图像对齐"),
        combo("Appearance/imageAlign", {QString::fromUtf8("左"), QString::fromUtf8("居中"),
            QString::fromUtf8("右")}, 1));
    form->addRow(QString::fromUtf8("标签排列"),
        combo("Appearance/labelAlign", {QString::fromUtf8("左"), QString::fromUtf8("居中"),
            QString::fromUtf8("右")}, 1));
    form->addRow(chk("Appearance/formatColor", QString::fromUtf8("文件根据格式显示以下颜色(文件名底色)"), true));
    form->addRow(new QLabel(QString::fromUtf8(
        "标签颜色列表(扩展名 → 底色):\n"
        "  gif = rgb(170,170,0)\n"
        "  mp4/mov/mkv/3gp/amr/asf/avi/bik/dsm/f4v/flc/flv\n"
        "  ifo/m2t/m4v/mts/mpeg/ogv/rm/rmvb/swf/ts/webm/wmv = rgb(170,85,0)\n"
        "(颜色编辑器即将支持)")));
    return wrapTitled(QString::fromUtf8("外观"), form);
}

// ── 标签颜色页:扩展名列表整行底色填充,选中变蓝;右列输入/新建/移除/改色;底部默认色 ──
QWidget* SettingsDialog::pageLabelColors() {
    auto* root = new QVBoxLayout;
    root->setSpacing(10);

    root->addWidget(chk("Appearance/formatColor",
                        QString::fromUtf8("文件根据格式显示以下颜色(文件名底色)"), true));

    auto* body = new QHBoxLayout;
    body->setSpacing(12);

    // 左:扩展名列表(整行以对应颜色填充)
    auto* list = new QListWidget;
    list->setFixedWidth(320);
    list->setStyleSheet(
        "QListWidget{background:#151515;border:1px solid #3C3C3C;outline:none;}"
        "QListWidget::item{height:24px;padding:0 8px;border:none;}"
        "QListWidget::item:selected{background:#2F65C5;color:#FFFFFF;border:none;}");
    body->addWidget(list, 1);

    // 右:输入行 + 色块/按钮列
    auto* right = new QVBoxLayout;
    right->setSpacing(8);
    auto* inRow = new QHBoxLayout;
    inRow->addWidget(new QLabel(QString::fromUtf8("输入扩展名:")));
    auto* extEdit = new QLineEdit;
    extEdit->setPlaceholderText(QString::fromUtf8("gif"));
    extEdit->setFixedWidth(120);
    inRow->addWidget(extEdit);
    inRow->addSpacing(8);
    inRow->addWidget(new QLabel(QString::fromUtf8("或选一种格式:")));
    auto* extCombo = new QComboBox;
    {
        QStringList exts;
        for (const QString& e : IMAGE_EXTS)  exts << e.mid(1).toUpper();
        for (const QString& e : VIDEO_EXTS)  exts << e.mid(1).toUpper();
        for (const QString& e : AUDIO_EXTS)  exts << e.mid(1).toUpper();
        exts.removeDuplicates();
        std::sort(exts.begin(), exts.end());
        extCombo->addItems(exts);
    }
    inRow->addWidget(extCombo, 1);
    right->addLayout(inRow);

    // 色块行:左块=当前选中项颜色(点击改色) 右块=白色快选
    auto* swRow = new QHBoxLayout;
    swRow->setAlignment(Qt::AlignRight);
    auto* colorBtn = new QToolButton;
    colorBtn->setFixedSize(30, 24);
    auto* whiteBtn = new QToolButton;
    whiteBtn->setFixedSize(30, 24);
    whiteBtn->setStyleSheet("background:#FFFFFF;border:1px solid #4A4A4A;");
    whiteBtn->setToolTip(QString::fromUtf8("设为白色"));
    swRow->addWidget(colorBtn);
    swRow->addWidget(whiteBtn);
    right->addLayout(swRow);

    auto* addBtn = new QPushButton(QString::fromUtf8("新建"));
    auto* removeBtn = new QPushButton(QString::fromUtf8("移除"));
    right->addWidget(addBtn);
    right->addWidget(removeBtn);
    right->addStretch(1);

    body->addLayout(right);
    body->addStretch(1);
    root->addLayout(body, 1);

    // 底部:默认颜色(未列出格式的底色)
    auto* defRow = new QHBoxLayout;
    defRow->addWidget(new QLabel(QString::fromUtf8("默认颜色")));
    auto* defBtn = new QToolButton;
    defBtn->setFixedSize(30, 24);
    defBtn->setStyleSheet(QString("background:%1;border:1px solid #4A4A4A;")
                              .arg(LabelColors::fallbackColor().name()));
    defRow->addWidget(defBtn);
    defRow->addStretch(1);
    root->addLayout(defRow);

    // ── 数据流 ──
    auto refill = [list, colorBtn]() {
        QListWidgetItem* sel = list->currentItem();
        QString selExt = sel ? sel->text() : QString();
        list->blockSignals(true);
        list->clear();
        const auto entries = LabelColors::all();
        for (const auto& pair : entries) {
            const QString& ext = pair.first;
            const QColor& col = pair.second;
            auto* it = new QListWidgetItem(ext);
            it->setBackground(col);
            it->setForeground(col.lightness() > 140 ? QColor("#000000") : QColor("#FFFFFF"));
            list->addItem(it);
            if (ext == selExt) list->setCurrentItem(it);
        }
        list->blockSignals(false);
        QColor cur = selExt.isEmpty() ? QColor("#191919")
                   : LabelColors::colorForExt(selExt);
        colorBtn->setStyleSheet(QString("background:%1;border:1px solid #4A4A4A;")
                                    .arg(cur.name()));
    };
    auto syncColorBtn = [list, colorBtn]() {
        QListWidgetItem* it = list->currentItem();
        QColor cur = it ? LabelColors::colorForExt(it->text()) : QColor("#191919");
        colorBtn->setStyleSheet(QString("background:%1;border:1px solid #4A4A4A;")
                                    .arg(cur.name()));
    };
    refill();

    connect(list, &QListWidget::itemSelectionChanged, this, syncColorBtn);
    connect(colorBtn, &QToolButton::clicked, this, [this, list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        QColor c = QColorDialog::getColor(LabelColors::colorForExt(it->text()),
                                          this, QString::fromUtf8("选择颜色"));
        if (!c.isValid()) return;
        LabelColors::set(it->text(), c);
        refill();
    });
    connect(whiteBtn, &QToolButton::clicked, this, [list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        LabelColors::set(it->text(), QColor("#FFFFFF"));
        refill();
    });
    connect(addBtn, &QPushButton::clicked, this, [extEdit, extCombo, list, refill]() {
        QString ext = extEdit->text().trimmed().toLower();
        if (ext.isEmpty()) ext = extCombo->currentText().trimmed().toLower();
        if (ext.isEmpty()) return;
        ext.remove(QRegularExpression("^[.*]+"));
        LabelColors::set(ext, LabelColors::fallbackColor());
        refill();
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text() == ext) { list->setCurrentRow(i); break; }
    });
    connect(removeBtn, &QPushButton::clicked, this, [list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        LabelColors::remove(it->text());
        refill();
    });
    connect(defBtn, &QToolButton::clicked, this, [defBtn, refill]() {
        QColor c = QColorDialog::getColor(LabelColors::fallbackColor(),
                                          nullptr, QString::fromUtf8("默认颜色"));
        if (!c.isValid()) return;
        LabelColors::setFallbackColor(c);
        defBtn->setStyleSheet(QString("background:%1;border:1px solid #4A4A4A;")
                                  .arg(c.name()));
        refill();
    });

    return wrapTitled(QString::fromUtf8("标签颜色"), root);
}

QWidget* SettingsDialog::pageViewer() {
    auto* root = new QVBoxLayout;
    root->setSpacing(12);

    // 分组"缩放"
    auto* fZoom = new QFormLayout;
    fZoom->setVerticalSpacing(10);
    fZoom->addRow(QString::fromUtf8("自动缩放"),
        combo("Viewer/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高"),
            QString::fromUtf8("适应桌面"), QString::fromUtf8("窗口适应到图像")}, 2));
    fZoom->addRow(chk("Viewer/resetAutoOnNav", QString::fromUtf8("使用下一个/上一个文件重置'自动图像尺寸'设置"), false));
    fZoom->addRow(QString::fromUtf8("缩放率"),
        combo("Viewer/zoomMode", {QString::fromUtf8("固定"), QString::fromUtf8("变动")}, 1));
    fZoom->addRow(QString::fromUtf8("缩小抗锯齿"),
        combo("Viewer/outZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    fZoom->addRow(QString::fromUtf8("放大抗锯齿"),
        combo("Viewer/inZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    fZoom->addRow(chk("Viewer/hidpiPixel", QString::fromUtf8("在 HiDPI 屏幕上缩放:1 图像像素 = 1 屏幕像素"), false));
    fZoom->addRow(QString::fromUtf8("像素比"),
        combo("Viewer/pixelRatio", {"1.00 正方形", "0.91 D1/DV NTSC", "0.95 D4/D16 Standard",
            "1.09 D1/DV PAL", "1.20 D1/DV NTSC Widescreen", "1.33 HDV 1080/DVCPRO HD 720",
            "1.46 D1/DV PAL Widescreen", "1.50 DVCPRO HD 1080", "1.90 D4/D16 非变形",
            "2.00 变形"}, 0));
    root->addWidget(group(QString::fromUtf8("缩放"), fZoom));

    // 分组"背景与界面元素"
    auto* fUI = new QFormLayout;
    fUI->setVerticalSpacing(10);
    fUI->addRow(QString::fromUtf8("背景色"),
                colorPick("Viewer/backColor", "#000000"));
    fUI->addRow(chk("Viewer/checkerMode", QString::fromUtf8("背景以挡板模式显示"), false));
    fUI->addRow(chk("Viewer/showBorder", QString::fromUtf8("显示边框"), false));
    fUI->addRow(chk("Viewer/highlightSelection", QString::fromUtf8("显示高亮选择内容"), true));
    fUI->addRow(chk("Viewer/panTool", QString::fromUtf8("显示平移工具"), true));
    fUI->addRow(chk("Viewer/showRating", QString::fromUtf8("显示评级&标签颜色"), true));
    fUI->addRow(chk("Viewer/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    fUI->addRow(QString::fromUtf8("选中的"),
        combo("Viewer/selectedOverlay", {QString::fromUtf8("正常"),
            QString::fromUtf8("三分法"), QString::fromUtf8("黄金分割(Phi)")}, 0));
    root->addWidget(group(QString::fromUtf8("背景与界面元素"), fUI));
    return wrapTitled(QString::fromUtf8("查看"), root);
}

// ── 查看 → 其他:播放与性能(从查看页拆出,页面不再过长) ──
QWidget* SettingsDialog::pageViewerOther() {
    auto* fPlay = new QFormLayout;
    fPlay->setVerticalSpacing(10);
    fPlay->addRow(chk("Viewer/autoPlayVideo", QString::fromUtf8("自动播放(视频)"), true));
    fPlay->addRow(chk("Viewer/loopVideo", QString::fromUtf8("循环视频播放"), false));
    fPlay->addRow(chk("Viewer/autoPlayAudioCompanion", QString::fromUtf8("自动播放音频伴侣文件"), false));
    fPlay->addRow(chk("Viewer/loopFileList", QString::fromUtf8("循环文件列表"), false));
    fPlay->addRow(chk("Viewer/twoPassRender", QString::fromUtf8("加载时两段式渲染"), false));
    fPlay->addRow(chk("Viewer/readAhead", QString::fromUtf8("预先读取一幅图像"), true));
    fPlay->addRow(chk("Viewer/cacheBehind", QString::fromUtf8("保持当前图像"), true));
    fPlay->addRow(chk("Viewer/disableAnimation", QString::fromUtf8("禁用 GIF/JIF/APNG/ANI 动画"), false));
    fPlay->addRow(chk("Viewer/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    fPlay->addRow(chk("Viewer/sharpen", QString::fromUtf8("使用锐化 50%"), false));

    auto* root = new QVBoxLayout;
    root->setSpacing(12);
    root->addWidget(group(QString::fromUtf8("播放与性能"), fPlay));
    return wrapTitled(QString::fromUtf8("其他"), root);
}

QWidget* SettingsDialog::pageFullscreen() {
    auto* root = new QVBoxLayout;
    root->setSpacing(12);

    // 分组"显示"
    auto* fShow = new QFormLayout;
    fShow->setVerticalSpacing(10);
    fShow->addRow(QString::fromUtf8("自动缩放"),
        combo("Fullscreen/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高")}, 2));
    fShow->addRow(chk("Fullscreen/showPlaybar", QString::fromUtf8("显示播放条"), true));
    fShow->addRow(chk("Fullscreen/showInfo", QString::fromUtf8("显示信息"), true));
    fShow->addRow(chk("Fullscreen/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    fShow->addRow(chk("Fullscreen/showToolbar", QString::fromUtf8("显示工具栏"), false));
    fShow->addRow(chk("Fullscreen/hideCursor", QString::fromUtf8("隐藏鼠标箭头"), true));
    root->addWidget(group(QString::fromUtf8("显示"), fShow));

    // 分组"其他"
    auto* fMisc = new QFormLayout;
    fMisc->setVerticalSpacing(10);
    fMisc->addRow(QString::fromUtf8("背景色"),
                  colorPick("Fullscreen/backColor", "#000000"));
    fMisc->addRow(chk("Fullscreen/dualMonitor", QString::fromUtf8("双显示器:使用第二显示器"), false));
    fMisc->addRow(chk("Fullscreen/floatView", QString::fromUtf8("浮动视图(鼠标移动到屏幕顶侧或右侧时出现)"), true));
    root->addWidget(group(QString::fromUtf8("其他"), fMisc));
    return wrapTitled(QString::fromUtf8("全屏"), root);
}

QWidget* SettingsDialog::pageCache() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(chk("Cache/useCatalog", QString::fromUtf8("启用缓存目录"), true));
    form->addRow(chk("Cache/thumbInDB", QString::fromUtf8("允许缓存缩略图"), true));
    form->addRow(QString::fromUtf8("压缩"),
        combo("Cache/compression", {QString::fromUtf8("无"),
            QString::fromUtf8("无损 - ZIP 压缩"), QString::fromUtf8("有损高品质(JPEG)"),
            QString::fromUtf8("低品质(JPEG)"), QString::fromUtf8("低质量 - 高质量 (WebP)")}, 4));
    form->addRow(chk("Cache/maxCacheOn", QString::fromUtf8("缓存缩略图最大容量(MB)"), true));
    form->addRow(spin("Cache/maxCacheMB", 64, 10240, 500));
    form->addRow(QString::fromUtf8("数据库引擎的内存占用(MB)"),
        spin("Cache/dbCacheMB", 8, 8192, 64));
    form->addRow(chk("Cache/checkOnStartup", QString::fromUtf8("启动时检查缓存的完整性"), false));
    return wrapTitled(QString::fromUtf8("缓存数据库"), form);
}

// 窄列中段省略(C:\Use...der\ 形式;列宽可拖动配合)
class ElideMiddleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& i) const override {
        QStyledItemDelegate::initStyleOption(o, i);
        o->textElideMode = Qt::ElideMiddle;
    }
};

// ── 维护页:缩略图库统计 + 四列目录表(可拖列宽/中段省略) + 操作按钮 ──
QWidget* SettingsDialog::pageMaintenance() {
    auto* root = new QVBoxLayout;
    root->setSpacing(10);

    // 数据库统计行
    auto* summary = new QLabel;
    summary->setStyleSheet("color:#D0D0D0;background:transparent;");
    root->addWidget(summary);

    // 筛选框
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(QString::fromUtf8("筛选"));
    root->addWidget(filter);

    // 四列目录表:列宽可拖动,窄列中段省略
    auto* table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({QString::fromUtf8("缓存目录"),
        QString::fromUtf8("文件"), QString::fromUtf8("元数据"),
        QString::fromUtf8("缩略图")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setItemDelegate(new ElideMiddleDelegate(table));
    table->setStyleSheet(
        "QTableWidget{background:#151515;color:#FFFFFF;border:1px solid #3C3C3C;}"
        "QHeaderView::section{background:#1B1B1B;color:#FFFFFF;"
        "border:none;padding:4px;}");
    table->setColumnWidth(0, 260);
    table->setColumnWidth(1, 90);
    table->setColumnWidth(2, 110);
    root->addWidget(table, 1);

    // 按钮组(两行)
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(8);
    auto* delSelBtn = new QPushButton(QString::fromUtf8("删除"));
    auto* maintBtn = new QPushButton(QString::fromUtf8("维护..."));
    auto* syncBtn = new QPushButton(QString::fromUtf8("同步文件夹..."));
    row1->addWidget(delSelBtn);
    row1->addStretch();
    row1->addWidget(maintBtn);
    row1->addWidget(syncBtn);
    root->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->setSpacing(8);
    auto* delAllBtn = new QPushButton(QString::fromUtf8("删除全部"));
    auto* rebuildBtn = new QPushButton(QString::fromUtf8("重建缩略图"));
    row2->addWidget(delAllBtn);
    row2->addStretch();
    row2->addWidget(rebuildBtn);
    root->addLayout(row2);

    // ── 数据访问(thumbs 表:key/png/mtime/atime;独立连接名) ──
    auto db = []() -> QSqlDatabase {
        const QString conn = QStringLiteral("settings_maint_db");
        if (!QSqlDatabase::contains(conn)) {
            QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", conn);
            d.setDatabaseName(QCoreApplication::applicationDirPath() + "/thumbnails.db");
            d.open();
        }
        return QSqlDatabase::database(conn);
    };

    auto reload = [db, summary, table, filter]() {
        QSqlDatabase d = db();
        struct Rec { int count = 0; qint64 meta = 0; qint64 thumb = 0; };
        QHash<QString, Rec> byDir;
        qint64 totalThumb = 0, totalMeta = 0;
        int total = 0;
        QSqlQuery q(d);
        // 元数据字节 = key 长度 + mtime/atime 两个 double(估算)
        if (q.exec("SELECT key, LENGTH(png), LENGTH(key) FROM thumbs")) {
            while (q.next()) {
                const QString p = q.value(0).toString();
                const qint64 thumbB = q.value(1).toLongLong();
                const qint64 metaB = q.value(2).toLongLong() + 16;
                totalThumb += thumbB;
                totalMeta += metaB;
                ++total;
                int slash = p.lastIndexOf('/');
                if (slash < 0) slash = p.lastIndexOf('\\');
                QString dir = slash > 0 ? p.left(slash + 1) : p;
                Rec& r = byDir[dir];
                r.count += 1;
                r.meta += metaB;
                r.thumb += thumbB;
            }
        }
        QFileInfo fi(d.databaseName());
        summary->setText(QString::fromUtf8(
            "数据库 [目录:%1  →  元数据:%2  →  缩略图:%3]")
            .arg(QString::asprintf("%.2f MB", (fi.size() + totalMeta) / 1048576.0))
            .arg(QString::asprintf("%.2f MB", totalMeta / 1048576.0))
            .arg(QString::asprintf("%.2f MB", totalThumb / 1048576.0)));

        QVector<QPair<QString, Rec>> rows;
        for (auto it = byDir.constBegin(); it != byDir.constEnd(); ++it)
            rows.append({it.key(), it.value()});
        std::sort(rows.begin(), rows.end(),
                  [](auto& a, auto& b) { return a.second.thumb > b.second.thumb; });

        const QString f = filter->text().trimmed();
        table->setRowCount(0);
        for (const auto& row : rows) {
            if (!f.isEmpty() && !row.first.contains(f, Qt::CaseInsensitive)) continue;
            int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(row.first));
            table->setItem(r, 1, new QTableWidgetItem(QString::number(row.second.count)));
            table->setItem(r, 2, new QTableWidgetItem(
                QString::asprintf("%.2f KB", row.second.meta / 1024.0)));
            table->setItem(r, 3, new QTableWidgetItem(
                QString::asprintf("%.2f KB", row.second.thumb / 1024.0)));
        }
    };

    connect(filter, &QLineEdit::textChanged, this, reload);
    connect(delSelBtn, &QPushButton::clicked, this, [this, table, db, reload]() {
        auto sel = table->selectedItems();
        if (sel.isEmpty()) return;
        QString dir = table->item(sel.first()->row(), 0)->text();
        if (QMessageBox::question(this, QString::fromUtf8("删除"),
            QString::fromUtf8("删除该目录的全部缓存条目?\n%1").arg(dir))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.prepare("DELETE FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        reload();
    });
    connect(delAllBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, QString::fromUtf8("删除全部"),
            QString::fromUtf8("确认清空全部缩略图缓存?(浏览时会自动重建)"))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.exec("DELETE FROM thumbs");
        reload();
    });
    connect(rebuildBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, QString::fromUtf8("重建缩略图"),
            QString::fromUtf8("清空缓存后,下次浏览文件夹时将按当前设置自动重建缩略图。继续?"))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.exec("DELETE FROM thumbs");
        reload();
    });
    // 同步文件夹:从库中删除"文件已不存在"的孤立条目
    connect(syncBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, QString::fromUtf8("缓存数据库 - 同步目录"),
            QString::fromUtf8("警告!\n此操作将从缓存数据库中删除全部的孤立条目。\n是否继续?"),
            QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) return;
        QSqlDatabase d = db();
        QStringList gone;
        QSqlQuery q(d);
        if (q.exec("SELECT key FROM thumbs")) {
            while (q.next()) {
                const QString p = q.value(0).toString();
                if (!QFileInfo::exists(p)) gone << p;
            }
        }
        d.transaction();
        QSqlQuery del(d);
        del.prepare("DELETE FROM thumbs WHERE key = ?");
        for (const auto& p : gone) { del.addBindValue(p); del.exec(); }
        d.commit();
        QMessageBox::information(this, QString::fromUtf8("同步目录"),
            QString::fromUtf8("已移除 %1 条孤立条目。").arg(gone.size()));
        reload();
    });
    // 维护...:优化数据库(VACUUM)/核对全目录/清除缩略图
    connect(maintBtn, &QPushButton::clicked, this, [this, db, reload]() {
        QDialog dlg(this);
        dlg.setWindowTitle(QString::fromUtf8("缓存维护"));
        dlg.setFixedWidth(320);
        auto* v = new QVBoxLayout(&dlg);
        auto* optChk = new QCheckBox(QString::fromUtf8("优化数据库(处理时间长)"));
        auto* lblClean = new QLabel(QString::fromUtf8("清理"));
        auto* scanChk = new QCheckBox(QString::fromUtf8("核对全目录(移除孤立条目)"));
        scanChk->setChecked(true);
        auto* lblPurge = new QLabel(QString::fromUtf8("清除"));
        auto* thumbChk = new QCheckBox(QString::fromUtf8("清除缩略图"));
        for (auto* w : std::vector<QWidget*>{ optChk, lblClean, scanChk, lblPurge, thumbChk }) {
            if (auto* c = qobject_cast<QCheckBox*>(w)) c->setMinimumHeight(24);
            v->addWidget(w);
        }
        auto* btns = new QHBoxLayout;
        btns->addStretch();
        auto* runBtn = new QPushButton(QString::fromUtf8("运行"));
        // 显式默认:否则 Enter 与"空格=确认"按**创建顺序**挑按钮(全靠 runBtn 恰好先建)
        runBtn->setDefault(true);
        auto* cancelBtn = new QPushButton(QString::fromUtf8("取消"));
        btns->addWidget(runBtn);
        btns->addWidget(cancelBtn);
        v->addLayout(btns);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        connect(runBtn, &QPushButton::clicked, &dlg, [&]() {
            QSqlDatabase d = db();
            if (optChk->isChecked()) {
                QSqlQuery q(d);
                q.exec("VACUUM");
            }
            if (scanChk->isChecked()) {
                QStringList gone;
                QSqlQuery q(d);
                if (q.exec("SELECT key FROM thumbs")) {
                    while (q.next()) {
                        const QString p = q.value(0).toString();
                        if (!QFileInfo::exists(p)) gone << p;
                    }
                }
                d.transaction();
                QSqlQuery del(d);
                del.prepare("DELETE FROM thumbs WHERE key = ?");
                for (const auto& p : gone) { del.addBindValue(p); del.exec(); }
                d.commit();
            }
            if (thumbChk->isChecked()) {
                QSqlQuery q(d);
                q.exec("DELETE FROM thumbs");
            }
            dlg.accept();
            reload();
        });
        dlg.exec();
    });

    reload();
    return wrapTitled(QString::fromUtf8("维护"), root);
}

QWidget* SettingsDialog::pageIntegration() {
    auto* form = new QFormLayout;

    // "用 xnnview 浏览"右键菜单(勾选即写 HKCU 注册表,免管理员)
    auto* browseChk = new QCheckBox(
        QString::fromUtf8("将\"用 xnnview 浏览\"添加到系统右键菜单(HKCU,免管理员)"));
    browseChk->setChecked(Integration::isBrowseMenuInstalled());
    connect(browseChk, &QCheckBox::toggled, this, [](bool on) {
        bool ok = on ? Integration::addBrowseContextMenu()
                     : Integration::removeBrowseContextMenu();
        QMessageBox::information(nullptr, QString::fromUtf8("系统集成"),
            ok ? QString::fromUtf8(on ? "已添加右键菜单,资源管理器中即时生效。"
                                      : "已移除右键菜单。")
               : QString::fromUtf8("注册表写入失败。"));
    });
    form->addRow(browseChk);

    form->addRow(chk("Integration/shellMenu",
        QString::fromUtf8("添加 shell 至右键菜单"), true));

    auto* regBtn = new QPushButton(QString::fromUtf8("注册应用(加入\"打开方式\"列表)"));
    connect(regBtn, &QPushButton::clicked, this, []() {
        bool ok = Integration::registerOpenWith();
        QMessageBox::information(nullptr, QString::fromUtf8("注册应用"),
            ok ? QString::fromUtf8("已注册。右键文件 → 打开方式 中可选 xnnview。")
               : QString::fromUtf8("注册失败。"));
    });
    form->addRow(QString::fromUtf8("文件关联"), regBtn);

    auto* defAppBtn = new QPushButton(QString::fromUtf8("打开\"默认应用程序\"设置"));
    connect(defAppBtn, &QPushButton::clicked, this, []() {
        QProcess::startDetached("ms-settings:defaultapps");
    });
    form->addRow(QString::fromUtf8("默认应用"), defAppBtn);

    form->addRow(QString::fromUtf8("配置文件 ini 路径"),
        combo("Integration/iniLocation", {QString::fromUtf8("程序文件夹(便携)"),
            QString::fromUtf8("系统文件夹(%APPDATA%)(重启后生效,即将支持)")}, 0));
    form->addRow(new QLabel(QString::fromUtf8("当前 ini:") + AppSettings::instance().iniPath()));
    return wrapTitled(QString::fromUtf8("系统集成"), form);
}
