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

// ═══ 页面 ═══
QWidget* SettingsDialog::pageGeneral() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(chk("General/singleInstance", gazeTr("仅允许运行一个 Gaze 程序实例"), false));
    form->addRow(chk("General/exifRotate", gazeTr("根据 EXIF 方向标签旋转图像"), true));
    // #122 接线完毕。默认**关**:关掉时"1:1"仍是 #95 定的"1 图像像素 = 1 屏幕像素"。
    // 勾上才按标称物理尺寸换算 —— #95 当年就是因为这条乘法默认生效,
    // 72dpi 截图被画得比"适应窗口"还小,才把 1:1 收回纯像素口径的。
    QWidget* wDpi = chk("General/exifDpi", gazeTr("1:1 按文件 DPI 显示物理尺寸"), false);
    wDpi->setToolTip(gazeTr(
        "勾上后\"1:1 / 长按看原图\"按文件自带 DPI 换算(屏幕DPI ÷ 图像DPI),\n"
        "显示的是标称物理尺寸;文件没写 DPI 时仍按纯像素 1:1。\n"
        "不勾 = 1 图像像素 : 1 屏幕像素(#95 口径,默认)。"));
    form->addRow(wDpi);
    QWidget* wAdj = chk("General/dpiAdjust", gazeTr("若 X/Y DPI 不相等,调整缩放"), true);
    wAdj->setToolTip(gazeTr(
        "只在上一项勾选时参与:X/Y DPI 不等时横轴按各自 DPI 换算,\n"
        "免得非正方形像素的图(少数 TIFF/BMP)被拉变形。"));
    form->addRow(wAdj);
    form->addRow(gazeTr("退出程序时保存会话"),
        combo("General/saveSession", {gazeTr("从不"), gazeTr("询问"), gazeTr("始终")}, 2));
    return wrapTitled(gazeTr("常规"), form);
}

QWidget* SettingsDialog::pageStartup() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(gazeTr("带文件启动"),
        combo("Start/withFile", {gazeTr("查看器"), gazeTr("全屏 - 查看器"),
                                 gazeTr("浏览器"), gazeTr("浏览器 - 全屏"),
                                 gazeTr("全屏预览")}, 0));
    auto* wof = combo("Start/withoutFile", {gazeTr("无"), gazeTr("上次使用的目录"),
                                            gazeTr("指定目录")}, 1);
    form->addRow(gazeTr("不带文件启动"), wof);
    // #106:Browser/startDir 此前只有读点(启动分支)没有写点,"指定目录"是死选项
    auto* row = new QWidget;
    auto* hl = new QHBoxLayout(row);
    hl->setContentsMargins(0, 0, 0, 0);
    auto* ed = edit("Browser/startDir", QString());
    ed->setToolTip(gazeTr("「不带文件启动」选「指定目录」时,浏览器从这里打开"));
    hl->addWidget(ed, 1);
    auto* btn = new QToolButton(row);
    btn->setText(gazeTr("浏览…"));
    hl->addWidget(btn);
    form->addRow(gazeTr("启动目录"), row);
    connect(btn, &QToolButton::clicked, this, [this, ed]() {
        const QString d = QFileDialog::getExistingDirectory(this, gazeTr("选择启动目录"), ed->text());
        if (d.isEmpty()) return;
        ed->setText(d);   // edit() 挂在 textChanged 上,改文本即落盘
    });
    connect(wof, &QComboBox::currentIndexChanged, this, [row](int idx) { row->setEnabled(idx == 2); });
    row->setEnabled(wof->currentIndex() == 2);
    form->addRow(chk("Start/rememberFilename", gazeTr("记录选择的文件名"), true));
    return wrapTitled(gazeTr("启动"), form);
}

QWidget* SettingsDialog::pageFileOps() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(chk("FileOps/confirmDelete", gazeTr("文件删除前确认"), true));
    form->addRow(chk("FileOps/confirmDeleteDirs",
        gazeTr("删除含文件夹时确认(不受上一项影响)"), true));
    form->addRow(chk("FileOps/useRecycleBin",
        gazeTr("使用回收站(关闭后永久删除,并强制确认)"), true));
    form->addRow(chk("FileOps/deleteToast",
        gazeTr("删除后在左下角显示提示"), true));
    auto* toastMs = spin("FileOps/toastMs", 400, 8000, 1500);
    toastMs->setSingleStep(100);
    toastMs->setSuffix(gazeTr(" 毫秒"));
    form->addRow(gazeTr("删除提示停留时长"), toastMs);
    // 拖放语义(用户 2026-08-31 明令):拖放=移动,Ctrl+拖放=复制;
    // 落到文件夹上都会改文件,是否弹窗确认由这一项控制
    form->addRow(chk("FileOps/dropConfirm",
        gazeTr("拖放改动文件前确认(移动/复制到文件夹时弹窗)"), true));
    form->addRow(chk("FileOps/losslessBackup", gazeTr("为无损翻转/旋转生成备份"), true));
    form->addRow(chk("FileOps/losslessKeepMeta", gazeTr("为无损翻转/旋转保留原始元数据"), true));
    form->addRow(chk("FileOps/renameDialog", gazeTr("使用对话框重命名文件/文件夹"), true));
    form->addRow(gazeTr("重复文件命名"),
        combo("FileOps/duplicateTemplate",
              {"<文件名>-(#)", "<文件名> - 副本 (#)", "<文件名>-副本 (#)", "<文件名>-#", "副本 (#) - <文件名>"}, 0));
    form->addRow(new QLabel(gazeTr(
        "说明:右键无损旋转/翻转会先复制原件再旋转,不修改任何元数据(含创建/修改时间)。")));
    return wrapTitled(gazeTr("文件操作"), form);
}

QWidget* SettingsDialog::pageInterface() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    auto* multiTabs = chk("Interface/multiViewerTabs",
                          gazeTr("同一文件多个查看器标签卡"), false);
    multiTabs->setToolTip(gazeTr(
        "开:同一个文件可以再开一张标签(右键\"在新标签卡中打开\"点两次就有两张)。\n"
        "关(默认):一个文件只占一张标签,重复打开就切回已有那张。"));
    form->addRow(multiTabs);
    auto* syncBrowser = chk("Interface/syncBrowser",
                            gazeTr("关闭视图时,同步调整浏览器"), false);
    syncBrowser->setToolTip(gazeTr(
        "开:切换查看器标签、以及退回浏览器时,把文件列表的选中项挪到那个文件上,\n"
        "于是退回后高亮的就是刚才最后看的那张,标题栏与预览也都跟着它。\n"
        "关(默认):退回浏览器后列表仍停在你进查看器前的那一行。"));
    form->addRow(syncBrowser);
    auto* oneTab = chk("Interface/oneViewerTab",
                       gazeTr("一个文件仅有一个查看器标签卡"), false);
    oneTab->setToolTip(gazeTr(
        "开:查看器始终只保留一张标签,新打开的文件顶掉当前标签。\n"
        "关(默认):右键\"在新标签卡中打开\"每张另起一条,张数受下面的上限约束。"));
    form->addRow(oneTab);
    form->addRow(gazeTr("查看器标签卡上限(2-99,默认99)"),
        spin("Interface/maxViewerTabs", 2, 99, 99));
    auto* panesOnStart = chk("Interface/showPanesOnStart",
        gazeTr("启动时打开文件列表和预览框"), true);
    panesOnStart->setToolTip(gazeTr(
        "勾选:启动时强制显示文件夹树与预览面板。\n"
        "不勾选:沿用上次退出时的面板开关(需保存布局或启用\"应用关闭时的布局\")。"));
    form->addRow(panesOnStart);
    form->addRow(gazeTr("最近的文件上限数量(0-100)"),
        spin("Interface/maxRecent", 0, 100, 20));
    form->addRow(chk("Interface/clearRecentOnExit", gazeTr("退出时清理\"最近的文件\"记录"), false));
    return wrapTitled(gazeTr("界面"), form);
}

// ── 标题栏页:浏览器模式/查看器两组,每组模板输入框+▶ 变量菜单 ──
static QWidget* titleTemplateRow(const QString& key, const QString& def);

QWidget* SettingsDialog::pageTitlebar() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距);#148 再收一档

    auto* fBr = new QFormLayout;
    fBr->setVerticalSpacing(6);
    fBr->addRow(titleTemplateRow("Interface/titleBrowser",
                                 QString::fromUtf8("{文件夹} - {文件名 含扩展名} - Gaze")));
    root->addWidget(group(gazeTr("浏览器模式"), fBr));

    auto* fVw = new QFormLayout;
    fVw->setVerticalSpacing(6);
    fVw->addRow(titleTemplateRow("Interface/titleViewer",
                                 QString::fromUtf8("{文件夹} - {文件名 含扩展名} - Gaze")));
    root->addWidget(group(gazeTr("查看器"), fVw));
    return wrapTitled(gazeTr("标题栏"), root);
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
    btn->setToolTip(gazeTr("插入变量"));
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
    auto* timeMenu = menu->addMenu(gazeTr("时间格式变量"));
    auto addTime = [timeMenu, e](const QString& var) {
        timeMenu->addAction(var, e, [e, var]() { e->insert("{" + var + "}"); });
    };
    auto addHeader = [timeMenu](const QString& text) {
        timeMenu->addAction(text)->setEnabled(false);
    };
    // 大写=修改时间,小写=创建时间;N/n=分钟(与 M/m=月 区分)
    addHeader(gazeTr("修改时间 年/月/日 时/分/秒"));
    for (const char* v : { "Y", "M", "D", "H", "N", "S" }) addTime(v);
    addHeader(gazeTr("创建时间 年/月/日 时/分/秒"));
    for (const char* v : { "y", "m", "d", "h", "n", "s" }) addTime(v);
    timeMenu->addSeparator();
    for (const char* v : { "Y-m-d_H-N-S", "Y_m_d_H_N_S",
                           "y-m-d_h-n-s", "y_m_d_h_n_s" }) addTime(v);
    QObject::connect(btn, &QToolButton::clicked, btn, [btn, menu]() {
        menu->exec(btn->mapToGlobal(QPoint(0, btn->height())));
    });
    return row;
}

QWidget* SettingsDialog::pageSwitchMode() {
    QStringList opts = QStringList{
        gazeTr("浏览器 ↔ 全屏 | 查看器 ↔ 全屏"),
        gazeTr("浏览器 ↔ 查看器"),
        gazeTr("浏览器 → 全屏 → 查看器"),
        gazeTr("浏览器 → 查看器 → 全屏"),
        gazeTr("什么都不做"),
        gazeTr("用系统程序打开")};
    auto* form = new QFormLayout;
    // 双击默认"浏览器↔查看器"(#102)。非图像/非视频(压缩包、文档、exe…不受
    // 这项管)一律由系统默认程序打开 —— 那种文件没有"模式"可切。
    form->addRow(gazeTr("双击"),
        combo("SwitchMode/doubleClick", opts, 1));
    form->addRow(gazeTr("中键"),
        combo("SwitchMode/middleClick", opts, 4));
    form->addRow(gazeTr("回车键"),
        combo("SwitchMode/enterKey", opts, 1));
    return wrapTitled(gazeTr("切换模式"), form);
}
