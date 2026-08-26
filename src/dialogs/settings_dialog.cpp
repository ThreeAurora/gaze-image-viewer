#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
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
    connect(e, &QLineEdit::textChanged, this, [key](const QString& v) {
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
    form->addRow(QString::fromUtf8("退出程序时保存会话"),
        combo("General/saveSession", {QString::fromUtf8("从不"), QString::fromUtf8("询问"), QString::fromUtf8("始终")}, 0));
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
    form->addRow(chk("FileOps/confirmDelete", QString::fromUtf8("文件删除前确认"), false));
    form->addRow(chk("FileOps/useRecycleBin", QString::fromUtf8("使用回收站(删除永远进回收站)"), true));
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
    form->addRow(chk("Interface/multiViewerTabs", QString::fromUtf8("同一文件多个查看器标签卡"), false));
    form->addRow(chk("Interface/syncBrowser", QString::fromUtf8("关闭视图时,同步调整浏览器"), false));
    form->addRow(chk("Interface/oneViewerTab", QString::fromUtf8("一个文件仅有一个查看器标签卡"), false));
    form->addRow(QString::fromUtf8("最近的文件上限数量(0-100)"),
        spin("Interface/maxRecent", 0, 100, 20));
    form->addRow(chk("Interface/clearRecentOnExit", QString::fromUtf8("退出时清理\"最近的文件\"记录"), false));
    form->addRow(QString::fromUtf8("标题栏 - 浏览器模式"),
        edit("Interface/titleBrowser", QString::fromUtf8("{路径}{文件名 含扩展名}")));
    form->addRow(QString::fromUtf8("标题栏 - 查看器"),
        edit("Interface/titleViewer", QString::fromUtf8("{路径}{文件名 含扩展名}")));
    form->addRow(new QLabel(QString::fromUtf8(
        "标题栏可用变量:{文件名} {文件名 含扩展名} {文件夹} {文件夹名} {大小}\n"
        "{创建日期} {修改日期} {评级} {颜色标签} 及时间变量 Y y m d H M S 等")));
    return wrapTitled(QString::fromUtf8("界面"), form);
}

QWidget* SettingsDialog::pageKeyboardMouse() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(12);
    form->addRow(QString::fromUtf8("左/右键方向键"),
        combo("Keyboard/leftRight", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("水平滚动"), QString::fromUtf8("自动"),
            QString::fromUtf8("上一帧/下一帧")}, 0));
    form->addRow(QString::fromUtf8("上/下方向键"),
        combo("Keyboard/upDown", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("向上/向下滚动"), QString::fromUtf8("自动"),
            QString::fromUtf8("上一帧/下一帧")}, 0));
    form->addRow(QString::fromUtf8("空格"),
        combo("Keyboard/space", {QString::fromUtf8("什么都不做"),
            QString::fromUtf8("下一个文件"), QString::fromUtf8("快速幻灯片")}, 0));
    form->addRow(chk("Keyboard/escCloseBrowser", QString::fromUtf8("按 ESC 关闭:浏览器模式"), false));
    form->addRow(chk("Keyboard/escCloseViewer", QString::fromUtf8("按 ESC 关闭:查看器"), true));
    form->addRow(new QLabel(QString::fromUtf8("──── 鼠标 ────")));
    form->addRow(new QLabel(QString::fromUtf8(
        "左键(无/Ctrl/Alt/Shift 修饰)= 放缩与移动:\n"
        "  单击图片 → 缩放为原图大小并聚焦光标处,按住拖动看细节,松开还原。\n"
        "  Ctrl+滚轮缩放过 → 左键变为纯拖动;缩放幅度 1.25 倍步进。\n"
        "滚轮:无修饰/Alt/Shift = 上一个/下一个文件;Ctrl = 缩放。\n"
        "右键/中键 = 什么都不做(右键保留上下文菜单)。\n"
        "以上为强制行为,此处仅作说明。")));
    return wrapTitled(QString::fromUtf8("键盘和鼠标"), form);
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
        QString::fromUtf8("什么都不做")};
    auto* form = new QFormLayout;
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
        combo("Browser/previewBackColor", {QString::fromUtf8("黑色"), QString::fromUtf8("白色"),
            QString::fromUtf8("灰色")}, 0));
    fPrev->addRow(chk("Browser/showRating", QString::fromUtf8("显示评级(颜色标签)"), true));
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
    fCreate->addRow(QString::fromUtf8("视频文件提取帧位置(%)"),
        spin("Thumbs/videoFramePct", 0, 100, 20));
    root->addWidget(group(QString::fromUtf8("创建"), fCreate));

    // 分组"处理"
    auto* fProc = new QFormLayout;
    fProc->setVerticalSpacing(10);
    fProc->addRow(chk("Thumbs/alpha", QString::fromUtf8("使用alpha通道"), true));
    fProc->addRow(chk("Thumbs/transparencyGrid", QString::fromUtf8("使用透明网格"), true));
    fProc->addRow(chk("Thumbs/sharpen", QString::fromUtf8("锐化缩略图"), true));
    fProc->addRow(chk("Thumbs/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    root->addWidget(group(QString::fromUtf8("处理"), fProc));
    return wrapTitled(QString::fromUtf8("缩略图"), root);
}

QWidget* SettingsDialog::pageAppearance() {
    auto* form = new QFormLayout;
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 宽"),
        spin("Appearance/customThumbW", 32, 512, 96));
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 高"),
        spin("Appearance/customThumbH", 32, 512, 96));
    form->addRow(chk("Appearance/shadow", QString::fromUtf8("使用阴影"), false));
    form->addRow(QString::fromUtf8("边框粗细"), spin("Appearance/borderSize", 0, 10, 0));
    form->addRow(QString::fromUtf8("间距"), spin("Appearance/spacing", 0, 40, 4));
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
    return wrapPage(form);
}

QWidget* SettingsDialog::pageViewer() {
    auto* form = new QFormLayout;
    form->addRow(QString::fromUtf8("自动缩放"),
        combo("Viewer/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高"),
            QString::fromUtf8("适应桌面"), QString::fromUtf8("窗口适应到图像")}, 2));
    form->addRow(chk("Viewer/resetAutoOnNav", QString::fromUtf8("使用下一个/上一个文件重置'自动图像尺寸'设置"), false));
    form->addRow(QString::fromUtf8("背景色"),
        combo("Viewer/backColor", {QString::fromUtf8("黑色"), QString::fromUtf8("白色"),
            QString::fromUtf8("灰色")}, 0));
    form->addRow(chk("Viewer/checkerMode", QString::fromUtf8("背景以挡板模式显示"), false));
    form->addRow(chk("Viewer/showBorder", QString::fromUtf8("显示边框"), false));
    form->addRow(chk("Viewer/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    form->addRow(chk("Viewer/sharpen", QString::fromUtf8("使用锐化 50%"), false));
    form->addRow(QString::fromUtf8("像素比"),
        combo("Viewer/pixelRatio", {"1.00 正方形", "0.91 D1/DV NTSC", "0.95 D4/D16 Standard",
            "1.09 D1/DV PAL", "1.20 D1/DV NTSC Widescreen", "1.33 HDV 1080/DVCPRO HD 720",
            "1.46 D1/DV PAL Widescreen", "1.50 DVCPRO HD 1080", "1.90 D4/D16 非变形",
            "2.00 变形"}, 0));
    form->addRow(QString::fromUtf8("缩放率"),
        combo("Viewer/zoomMode", {QString::fromUtf8("固定"), QString::fromUtf8("变动")}, 1));
    form->addRow(QString::fromUtf8("缩小抗锯齿"),
        combo("Viewer/outZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    form->addRow(QString::fromUtf8("放大抗锯齿"),
        combo("Viewer/inZoomFilter", {"无", "Bilinear", "Bicubic", "Spline 16",
            "Spline 36", "Lanczos 3", "Lanczos 4"}, 1));
    form->addRow(chk("Viewer/hidpiPixel", QString::fromUtf8("在 HiDPI 屏幕上缩放:1 图像像素 = 1 屏幕像素"), false));
    form->addRow(chk("Viewer/highlightSelection", QString::fromUtf8("显示高亮选择内容"), true));
    form->addRow(chk("Viewer/panTool", QString::fromUtf8("显示平移工具"), true));
    form->addRow(chk("Viewer/showRating", QString::fromUtf8("显示评级&标签颜色"), true));
    form->addRow(chk("Viewer/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    form->addRow(new QLabel(QString::fromUtf8("──── 其他 ────")));
    form->addRow(QString::fromUtf8("选中的"),
        combo("Viewer/selectedOverlay", {QString::fromUtf8("正常"),
            QString::fromUtf8("三分法"), QString::fromUtf8("黄金分割(Phi)")}, 0));
    form->addRow(chk("Viewer/loopFileList", QString::fromUtf8("循环文件列表"), false));
    form->addRow(chk("Viewer/autoPlayVideo", QString::fromUtf8("自动播放(视频)"), true));
    form->addRow(chk("Viewer/loopVideo", QString::fromUtf8("循环视频播放"), false));
    form->addRow(chk("Viewer/autoPlayAudioCompanion", QString::fromUtf8("自动播放音频伴侣文件"), false));
    form->addRow(chk("Viewer/twoPassRender", QString::fromUtf8("加载时两段式渲染"), false));
    form->addRow(chk("Viewer/readAhead", QString::fromUtf8("预先读取一幅图像"), true));
    form->addRow(chk("Viewer/cacheBehind", QString::fromUtf8("保持当前图像"), true));
    form->addRow(chk("Viewer/disableAnimation", QString::fromUtf8("禁用 GIF/JIF/APNG/ANI 动画"), false));
    return wrapPage(form);
}

QWidget* SettingsDialog::pageFullscreen() {
    auto* form = new QFormLayout;
    form->addRow(QString::fromUtf8("自动缩放"),
        combo("Fullscreen/autoFit", {QString::fromUtf8("上次使用过的"), QString::fromUtf8("不缩放"),
            QString::fromUtf8("适应窗口"), QString::fromUtf8("适应窗口大小 (仅小图片)"),
            QString::fromUtf8("适应窗口大小 (仅大图片)"), QString::fromUtf8("适应窗口宽度"),
            QString::fromUtf8("适应窗口高度"), QString::fromUtf8("适应窗口宽或高")}, 2));
    form->addRow(chk("Fullscreen/showPlaybar", QString::fromUtf8("显示播放条"), true));
    form->addRow(chk("Fullscreen/showInfo", QString::fromUtf8("显示信息"), true));
    form->addRow(chk("Fullscreen/showScrollbar", QString::fromUtf8("显示滚动条"), false));
    form->addRow(chk("Fullscreen/showToolbar", QString::fromUtf8("显示工具栏"), false));
    form->addRow(chk("Fullscreen/hideCursor", QString::fromUtf8("隐藏鼠标箭头"), true));
    form->addRow(QString::fromUtf8("背景色"),
        combo("Fullscreen/backColor", {QString::fromUtf8("黑色"), QString::fromUtf8("白色"),
            QString::fromUtf8("灰色")}, 0));
    form->addRow(chk("Fullscreen/dualMonitor", QString::fromUtf8("双显示器:使用第二显示器"), false));
    form->addRow(chk("Fullscreen/floatView", QString::fromUtf8("浮动视图(鼠标移动到屏幕顶侧或右侧时出现)"), true));
    return wrapPage(form);
}

QWidget* SettingsDialog::pageCache() {
    auto* form = new QFormLayout;
    form->addRow(chk("Cache/useCatalog", QString::fromUtf8("启用缓存目录"), true));
    form->addRow(chk("Cache/thumbInDB", QString::fromUtf8("允许缓存缩略图"), true));
    form->addRow(QString::fromUtf8("压缩"),
        combo("Cache/compression", {QString::fromUtf8("无"),
            QString::fromUtf8("无损 - ZIP 压缩"), QString::fromUtf8("有损高品质(JPEG)"),
            QString::fromUtf8("低品质(JPEG)"), QString::fromUtf8("低质量 - 高质量 (WebP)")}, 4));
    form->addRow(QString::fromUtf8("缩略图宽度"), spin("Cache/thumbWidth", 64, 1024, 465));
    form->addRow(QString::fromUtf8("缩略图高度"), spin("Cache/thumbHeight", 64, 1024, 365));
    form->addRow(chk("Cache/maxCacheOn", QString::fromUtf8("缓存缩略图最大容量(MB)"), true));
    form->addRow(spin("Cache/maxCacheMB", 64, 10240, 1024));
    form->addRow(QString::fromUtf8("数据库引擎的内存占用(MB)"),
        spin("Cache/dbCacheMB", 32, 8192, 1024));
    form->addRow(chk("Cache/checkOnStartup", QString::fromUtf8("启动时检查缓存的完整性"), false));
    form->addRow(new QLabel(QString::fromUtf8(
        "维护工具(统计/重建/同步/删除)见\"工具 → 缩略图数据库维护\"。")));
    return wrapPage(form);
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
    return wrapPage(form);
}
