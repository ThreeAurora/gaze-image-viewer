#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "previewpanel.h"
#include "imgsearchdialog.h"
#include "printdialog.h"
#include "infopanel.h"
#include "shelldelete.h"   // showDeleteToast:拖放复制成功的左下角提示
#include "sortheader.h"
#include "fileentry.h"
#include "livephoto.h"
#include "constants.h"
#include "thumbnailer.h"   // #105:查看器标签名左侧的小缩略图走同一缩略图管线
#include "validname.h"
#include "keytarget.h"
#include "logger.h"

#include <QMenuBar>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QToolBar>
#include <QComboBox>
#include <QMessageBox>
#include <QDialog>
#include <QAbstractButton>
#include <QFileInfo>
#include <QDir>
#include <QStandardPaths>
#include <QDateTime>
#include <QShortcut>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QApplication>
#include <QToolButton>
#include <QFrame>
#include <QStyle>
#include <QMenu>
#include <QActionGroup>
#include <QInputDialog>
#include <QSettings>
#include <QDesktopServices>
#include <QUrl>
#include <QTextEdit>
#include <QAbstractSpinBox>
#include <QDragEnterEvent>
#include <QDragMoveEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QClipboard>
#include <QPair>
#include <QProcess>
#include <QTimer>
#include "iconlib.h"
#include "i18n.h"
#include <QLabel>
#include "labelstore.h"
#include "settings_dialog.h"
#include "dbmaintenance.h"
#include "settings.h"

#include "mainwindow_internal.h"

void MainWindow::createMenubar() {
    auto *mb = menuBar();
    mb->setStyleSheet(QString::fromUtf8(
        "QMenuBar{background:%1;color:%2;font-size:12px;"
        "padding:3px 2px;border-bottom:1px solid %3;}"
        "QMenuBar::item{background:transparent;padding:4px 10px;border-radius:3px;}"
        "QMenuBar::item:selected{background:%4;}"
        "QMenuBar::item:pressed{background:%5;color:#FFF;}")
        .arg(C_MENUBAR, C_TEXT, C_SEPARATOR, C_CARD_HOVER, C_ACCENT));

    // ── 文件(F) ──
    auto *fileMenu = mb->addMenu(gazeTr("文件"));
    fileMenu->addAction(IconLib::appIcon("cmd_open"), gazeTr("打开"),
        QKeySequence("Ctrl+O"), this, [this]() {
            auto paths = m_fileGrid->selectedPaths();
            if (!paths.isEmpty()) openWithSystem(paths.first());
        });
    auto *recentMenu = fileMenu->addMenu(IconLib::appIcon("cmd_browse"), gazeTr("最近的文件"));
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        rebuildRecentMenu(recentMenu);
    });
    fileMenu->addAction(IconLib::appIcon("cmd_print"), gazeTr("打印..."),
        QKeySequence("Ctrl+P"), this, [this]() {
            // 有选中打选中,没选中打当前列表全部(和右键"打印"同一口径);
            // 夹在里面的文件夹/视频由 PrintDialog 按扩展名滤掉并如实提示
            QStringList paths = m_fileGrid->selectedPaths();
            if (paths.isEmpty())
                for (int i = 0; i < m_fileGrid->fileCount(); ++i)
                    paths << m_fileGrid->pathOf(i);
            PrintDialog::printImages(this, paths);
        });
    fileMenu->addSeparator();
    fileMenu->addAction(gazeTr("刷新(&R)"), QKeySequence("F5"), this, [this](){ refresh(); });
    fileMenu->addSeparator();
    fileMenu->addAction(gazeTr("退出(&X)"), QKeySequence("Alt+X"), this, &QWidget::close);

    // ── 编辑(E) ──
    auto *editMenu = mb->addMenu(gazeTr("编辑"));
    editMenu->addAction(IconLib::appIcon("cmd_copyPath"),
        gazeTr("复制绝对路径"), QKeySequence("Ctrl+Shift+C"), this, [this]() {
            auto paths = m_fileGrid->selectedPaths();
            if (!paths.isEmpty())
                QApplication::clipboard()->setText(paths.join("\n"));
        });
    // #136:重命名必须是一条**带 shortcut 的 QAction**，不能只在键盘过滤器里加分支 ——
    // 设置→交互→快捷键配置页(settings_pages_input.cpp 的 fillTable)只列"带非空 shortcut
    // 的 QAction"，而 applyShortcuts() 也只按 Shortcuts/<动作文本> 读 ini 覆盖。
    // 挂在过滤器里的 F2 从来进不了那张表，这正是用户要求「写入快捷键配置页」的原因。
    // 助记符用 &R(Rename)：Qt 的助记符只要求**同一菜单内**唯一，编辑菜单里没有别的 &R
    // (「刷新(&R)」在文件菜单，不冲突)。
    // 2026-09-02 用户定版：F2=重命名(F3 让给预览开关)，与资源管理器惯例一致。
    // F2 仍保留 eventFilter 里的带守卫硬编码(见 mainwindow_keys.cpp)，可配主键
    // 由此 QAction 承担 —— 改键后 ini 覆盖，F2 仍作固定别名可用。
    editMenu->addAction(IconLib::appIcon("cmd_rename"),
        gazeTr("重命名(&R)"), QKeySequence("F2"), this, &MainWindow::renameFocused);
    editMenu->addSeparator();
    editMenu->addAction(IconLib::appIcon("cmd_selectAllFile"),
        gazeTr("全选"), QKeySequence("Ctrl+A"), this, [this](){ m_fileGrid->selectAllEntries(); });
    editMenu->addAction(gazeTr("反选"), QKeySequence("Ctrl+I"), this, [this](){ m_fileGrid->selectInvert(); });
    editMenu->addSeparator();
    editMenu->addAction(gazeTr("全选文件"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindFiles); });
    editMenu->addAction(gazeTr("全选文件夹"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindDirs); });
    editMenu->addAction(gazeTr("全选图像"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindImages); });
    editMenu->addAction(gazeTr("全选视频"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindVideos); });
    editMenu->addAction(gazeTr("全选音频"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindAudio); });
    // 颜色标签:原"元数据"一级菜单下,按需求挪到编辑(元数据菜单随之删除)
    editMenu->addSeparator();
    auto *labelMenu = editMenu->addMenu(IconLib::appIcon("label_item"),
                                        gazeTr("设置颜色标签"));
    // #234:键位不再用"括号尾巴"写进标题,改用标题里的 \t —— QMenu 会把 \t 后的
    // 内容画成右对齐快捷键列。同样不挂 QAction::setShortcut:真挂上会连文本框里
    // 的键一起吞掉(#61),实际响应在 qApp 事件过滤器(那里有"该不该给文本框"的判断)
    struct { int c; QString name; } colors[] = {
        {1, gazeTr("红色\tCtrl+1")},
        {2, gazeTr("橙色\tCtrl+2")},
        {3, gazeTr("黄色\tCtrl+3")},
        {4, gazeTr("绿色\tCtrl+4")},
        {5, gazeTr("蓝色\tCtrl+5")},
    };
    for (auto& c : colors)
        labelMenu->addAction(c.name, this, [this, c](){ applyColorLabel(c.c); });
    labelMenu->addSeparator();
    labelMenu->addAction(gazeTr("取消颜色标记\tCtrl+0 / D"), this, [this](){ applyColorLabel(0); });

    // ── 查看(V) ──
    auto *viewMenu = mb->addMenu(gazeTr("查看"));
    auto* fsAct = viewMenu->addAction(IconLib::appIcon("cmd_fullscreen"),
        gazeTr("界面全屏"), QKeySequence("F11"), this, [this]() {
            if (m_fullView) { exitFullView(); return; }   // F11 也得把全屏预览整个退干净(#154)
            if (isFullScreen()) showNormal(); else enterFullscreen();
        });
    fsAct->setCheckable(true);
    // #234:全屏预览的 G 键用 \t 右对齐列显示(不挂 setShortcut——真挂上会连文本框
    // 里的 G 一起吞掉,#61;实际响应在 qApp 事件过滤器,那里有"该不该给文本框"的判断)
    auto* fullAct = viewMenu->addAction(gazeTr("全屏预览\tG"), this, [this]() {
        toggleFullView();
    });
    fullAct->setCheckable(true);
    // #154:全屏=窗口铺满(浏览器态),全屏预览=只铺画面 —— 弹出瞬间按实态重勾
    connect(viewMenu, &QMenu::aboutToShow, this, [this, fsAct, fullAct]() {
        fsAct->setChecked(isFullScreen() && !m_fullView);
        fullAct->setChecked(m_fullView);
        // #234:右列键位跟着热键表走(设置→快捷键改键/清空后菜单仍说真话)
        const QString v = AppSettings::instance().get(
            QStringLiteral("ViewerShortcut/全屏预览"), QStringLiteral("G")).toString();
        fullAct->setText(gazeTr("全屏预览")
                         + (v.isEmpty() ? QString() : QLatin1Char('\t') + v));
    });
    viewMenu->addSeparator();
    viewMenu->addMenu(createViewModeMenu(viewMenu))->setIcon(IconLib::appIcon("viewas"));
    viewMenu->addMenu(createSortMenu(viewMenu))->setIcon(IconLib::appIcon("sort"));
    viewMenu->addMenu(createFilterMenu(viewMenu))->setIcon(IconLib::appIcon("cmd_filter"));
    auto *thumbSizeMenu = viewMenu->addMenu(IconLib::appIcon("cmd_paneThumbs"),
                                            gazeTr("缩略图尺寸"));
    struct { int w; QString label; } sizes[] = {
        {64, "64x48"}, {85, "85x64"}, {92, "92x69"}, {96, "96x72"},
        {128, "128x96"}, {192, "192x144"}, {384, "384x288"}, {768, "768x576"},
    };
    for (auto& s : sizes) {
        QAction* a = thumbSizeMenu->addAction(s.label, this, [this, s](){ onSizeChanged(s.w); });
        a->setCheckable(true);
        a->setData(s.w);
    }
    {
        auto* a = thumbSizeMenu->addAction(gazeTr("自定义..."), this, [this]() {
            bool ok = false;
            AppSettings& st = AppSettings::instance();
            int def = qBound(THUMB_W_MIN, st.get("Appearance/customThumbW", 96).toInt(), THUMB_W_MAX);
            int v = QInputDialog::getInt(this, gazeTr("自定义缩略图尺寸"),
                gazeTr("宽度(像素):"), def, THUMB_W_MIN, THUMB_W_MAX, 8, &ok);
            if (!ok) return;
            onSizeChanged(v);   // setCardSize 负责落盘 customThumbW(外观页同一条目)
        });
        a->setCheckable(true);
        a->setData(-1);
    }
    // #107:重勾按当前实际卡宽算(卡宽被表头/列数等途径改走时也如实);
    // 图标/列表/详细三种查看方式没有"卡宽档位"概念,全部不勾 —— 不许谎报
    connect(thumbSizeMenu, &QMenu::aboutToShow, this, [thumbSizeMenu, this]() {
        const int vm = m_fileGrid->viewMode();
        if (vm == VM_ICONS || vm == VM_LIST || vm == VM_DETAILS) {
            for (QAction* a : thumbSizeMenu->actions())
                if (a->isCheckable()) a->setChecked(false);
            return;
        }
        const int w = m_fileGrid->cardW();
        bool preset = false;
        for (QAction* a : thumbSizeMenu->actions()) {
            if (!a->isCheckable() || a->data().toInt() < 0) continue;
            a->setChecked(a->data().toInt() == w);
            if (a->data().toInt() == w) preset = true;
        }
        for (QAction* a : thumbSizeMenu->actions())
            if (a->isCheckable() && a->data().toInt() < 0)
                a->setChecked(!preset);
    });
    // 2026-09-04 用户令:「显示隐藏文件」进查看菜单,打勾即显示。与设置→
    // 文件列表的勾选框同一个键 FileList/showHidden —— FileGrid 订阅设置变更
    // 自动重筛(胶片条 allFilePaths 同步尊重),这里只落盘;用 triggered(bool)
    // 而非 toggled:弹出菜单时的程序性重勾不该反向写盘。显示态即时按实勾。
    auto* hiddenAct = viewMenu->addAction(gazeTr("显示隐藏文件"), this,
                                          [this](bool on) {
        AppSettings::instance().set("FileList/showHidden", on);
    });
    hiddenAct->setCheckable(true);
    connect(viewMenu, &QMenu::aboutToShow, this, [hiddenAct]() {
        hiddenAct->setChecked(AppSettings::instance()
                                  .get("FileList/showHidden", true).toBool());
    });
    viewMenu->addSeparator();
    viewMenu->addAction(gazeTr("放大缩略图"), QKeySequence("Ctrl+="), this, [this](){ onThumbZoom(1); });
    viewMenu->addAction(gazeTr("缩小缩略图"), QKeySequence("Ctrl+-"), this, [this](){ onThumbZoom(-1); });
    m_actBack = viewMenu->addAction(gazeTr("后退"), QKeySequence("Alt+Left"), this, [this](){ goBack(); });
    m_actFwd  = viewMenu->addAction(gazeTr("前进"), QKeySequence("Alt+Right"), this, [this](){ goForward(); });
    viewMenu->addAction(gazeTr("上级目录"), QKeySequence("Backspace"), this, [this](){ goUp(); });

    // ── 布局(L) → 视图 ──(依次追加,顺序即"文件 编辑 查看 布局 视图 工具 帮助")
    createLayoutMenu();
    createViewMenu();

    // ── 工具(T) ──
    // #132:顺序按"先配置工具、再用工具"排 —— 设置 在 以文搜图 上面(用户令)。
    // #218:三个长驻工具窗一律非模态单例 —— exec() 的应用级模态会整个锁死主窗
    //(用户点名:设置开着时主窗右上角 X 与任务栏关闭都必须仍能点)。已开着就提到前台。
    // 关窗即析构(finished→deleteLater,QDialog 的取消/Esc 走 reject 只是 hide,
    // WA_DeleteOnClose 对它们不生效):槽位落空,下次全新实例重读设置。
    auto* toolMenu = mb->addMenu(gazeTr("工具"));
    toolMenu->addAction(IconLib::appIcon("cmd_options"),
        gazeTr("设置..."), QKeySequence("F12"), this, [this]() {
            if (m_settingsDlg) {
                m_settingsDlg->setWindowState(m_settingsDlg->windowState() & ~Qt::WindowMinimized);
                m_settingsDlg->show(); m_settingsDlg->raise(); m_settingsDlg->activateWindow();
                return;
            }
            m_settingsDlg = new SettingsDialog(this);
            connect(m_settingsDlg, &QDialog::finished, m_settingsDlg, &QDialog::deleteLater);
            m_settingsDlg->show();
        });
    toolMenu->addAction(IconLib::appIcon("cmd_search"),
        gazeTr("以文搜图..."), QKeySequence("Ctrl+Shift+F"), this, [this]() {
            if (m_imgSearchDlg) {
                m_imgSearchDlg->setWindowState(m_imgSearchDlg->windowState() & ~Qt::WindowMinimized);
                m_imgSearchDlg->show(); m_imgSearchDlg->raise(); m_imgSearchDlg->activateWindow();
                return;
            }
            m_imgSearchDlg = new ImageSearchDialog(this);
            connect(m_imgSearchDlg, &QDialog::finished, m_imgSearchDlg, &QDialog::deleteLater);
            m_imgSearchDlg->show();
        });
    toolMenu->addAction(IconLib::appIcon("cmd_editMetadata"),
        gazeTr("缩略图数据库维护..."), this, [this]() {
            if (m_dbMaintDlg) {
                m_dbMaintDlg->setWindowState(m_dbMaintDlg->windowState() & ~Qt::WindowMinimized);
                m_dbMaintDlg->show(); m_dbMaintDlg->raise(); m_dbMaintDlg->activateWindow();
                return;
            }
            m_dbMaintDlg = new DbMaintenanceDialog(this);
            connect(m_dbMaintDlg, &QDialog::finished, m_dbMaintDlg, &QDialog::deleteLater);
            m_dbMaintDlg->show();
        });
    // #123:原「批量重命名...」菜单项已删 —— 该功能在 TODO_ALL §9 否决清单(@153611)。

    // ── 帮助(H) ──
    // #218:信息框也非模态(堆上+finished→deleteLater,挂主窗为父——主窗关则随之
    // 销毁,应用照常退出);模态的 about/information 会把主窗按住不让点
    auto *helpMenu = mb->addMenu(gazeTr("帮助"));
    helpMenu->addAction(gazeTr("快捷键帮助(&K)"), this, [this](){
        auto* mb = new QMessageBox(QMessageBox::Information, gazeTr("快捷键帮助"),
            gazeTr("C / ← / ↑ — 上一个\n"
            "V / → / ↓ — 下一个\n"
            "空格 — 播放/暂停\n"
            "Ctrl+PgUp/PgDn — 切换左/右标签页\n"
            "Shift+PgUp/PgDn — 快退/快进(秒数见设置→键盘)\n"
            "按住右键+滚轮 — 缩放(等同 Ctrl+滚轮)\n"
            "双击预览区 — 浏览器:开查看器标签 / 查看器:关签回浏览器\n"
            "F5 — 刷新  F11 — 界面全屏\n"
            "G — 全屏预览(只铺画面;再按 G 或 ESC 完全回到原布局)\n"
            "Alt+←/→ — 后退/前进  Backspace — 上级\n"
            "Ctrl+1~5 — 设置颜色标记(红橙黄绿蓝)\n"
            "Ctrl+0 / D — 取消颜色标记\n"
            "F — 加红色标记\n"
            "F2 — 重命名(文件树与文件页都可用,改谁看焦点;可在设置→快捷键改)\n"
            "F3 — 预览面板开关\n"
            "Del / S — 删除选中  X — 新建文件夹\n"
            "Enter — 切换查看器/浏览器(设置→键盘)\n"
            "Ctrl+A — 全选  Ctrl+I — 反选  Ctrl+W — 关标签(全屏中=只退全屏)\n"
            "Ctrl+Shift+T — 恢复刚关的标签页\n"
            "Esc — 退出全屏\n"
            "拖放 — 移动到文件夹  Ctrl+拖放 — 复制(设置→文件操作可关确认弹窗)"),
            QMessageBox::Ok, this);
        connect(mb, &QDialog::finished, mb, &QDialog::deleteLater);
        mb->show();
    });
    helpMenu->addAction(gazeTr("关于(&A)"), this, [this](){
        auto* mb = new QMessageBox(QMessageBox::Information, gazeTr("关于 Gaze"),
            gazeTr("Gaze\n通用图片/文件资源管理器\n\n"
            "主要功能:\n"
            "· 图库浏览(文件夹树 + 缩略图网格 + 预览面板)\n"
            "· Live Photo / Motion Photo 动态照片自动播放\n"
            "· 图片/视频/音频预览,颜色标记与筛选\n"
            "· 图片查看器模式(Ctrl+滚轮缩放细节)\n\n"
            "版本 1.0 — C++ + Qt6"),
            QMessageBox::Ok, this);
        connect(mb, &QDialog::finished, mb, &QDialog::deleteLater);
        mb->show();
    });

    // ── 语言(2026-09-03 国际化)──
    // 2026-09-04 用户令:「语言」要排在「帮助」左边(insertMenu),不再最右;
    // 后建的布局菜单照旧追加在末尾。三项单选,切换写入 General/language 并
    // 征询重启;重启用 --restart 自启动(绕过单实例握手,见 main.cpp)。
    // 选中项即当前生效意图;中文系统默认=跟随系统→中文照旧。
    {
        auto *langMenu = new QMenu(gazeTr("语言"), this);
        langMenu->setToolTip(gazeTr("界面语言(切换后重启生效)"));
        mb->insertMenu(helpMenu->menuAction(), langMenu);
        auto *langGroup = new QActionGroup(langMenu);
        langGroup->setExclusive(true);
        // 菜单项文字直接以字面量出现在 gazeTr() 里(提取器只认字面量,
        // "间接传变量"的串扫不到);简体中文/English 两语言恒等,无需译文。
        struct { QString key; QString label; } langs[] = {
            {QStringLiteral("system"), gazeTr("跟随系统")},
            {QStringLiteral("zh"),     QStringLiteral("简体中文")},
            {QStringLiteral("en"),     QStringLiteral("English")},
        };
        const QString cur =
            AppSettings::instance().get("General/language", "system").toString();
        for (auto& it : langs) {
            QAction* a = langMenu->addAction(it.label);
            a->setCheckable(true);
            a->setData(it.key);
            a->setChecked(cur == it.key);
            langGroup->addAction(a);
            connect(a, &QAction::triggered, this,
                    [this, key = it.key]() {
                if (AppSettings::instance()
                        .get("General/language", "system").toString() == key)
                    return;
                AppSettings::instance().set("General/language", key);
                // 语言切换靠重启生效(整套 UI 串在构造期解析)。def 后带
                // --restart 重启,老进程退出,新进程不握手直接开窗。
                const QMessageBox::StandardButton rb = QMessageBox::question(
                    this, gazeTr("切换界面语言"),
                    gazeTr("界面语言将在重启后生效。立即重启吗？"),
                    QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
                if (rb == QMessageBox::Yes) {
                    QProcess::startDetached(
                        QCoreApplication::applicationFilePath(),
                        {QStringLiteral("--restart")});
                    // 2026-09-03 夜补漏:只启新进程不退出自己,会堆出多个
                    // Gaze 并存(旧窗口还留在任务栏)。先拉起新进程再退自己 ——
                    // 新进程 --restart 跳过单实例握手直接开窗,无竞态。
                    QTimer::singleShot(0, QCoreApplication::instance(),
                                       &QCoreApplication::quit);
                }
            });
        }
    }
}

// ═══════════════════════════════════════════
// 查看方式/排序/筛选 菜单(菜单栏与工具栏共用)
// ═══════════════════════════════════════════
QMenu* MainWindow::createViewModeMenu(QWidget* parent) {
    auto *m = new QMenu(gazeTr("查看方式"), parent);
    // 查看方式 7 种(批次 3 实装差异渲染;当前统一缩略图)
    QStringList modes = {
        gazeTr("缩略图"), gazeTr("缩略图 + 文件名"),
        gazeTr("缩略图 + 标签"), gazeTr("缩略图 + 详细"),
        gazeTr("图标"), gazeTr("列表"),
        gazeTr("详细信息"), gazeTr("瀑布流"),
    };
    int curMode = m_fileGrid ? m_fileGrid->viewMode() : VM_THUMBS_NAME;
    for (int i = 0; i < modes.size(); ++i) {
        QAction* a = m->addAction(modes[i]);
        a->setCheckable(true);
        a->setChecked(i == curMode);
        connect(a, &QAction::triggered, this, [this, a, m, i]() {
            for (auto* act : m->actions()) act->setChecked(act == a);
            m_fileGrid->setViewMode(i);
        });
    }
    return m;
}

QMenu* MainWindow::createSortMenu(QWidget* parent) {
    auto *m = new QMenu(gazeTr("排序"), parent);
    // 注释/自定义排序未上桌:没有对应数据模型(此前静默退化成文件名排序,
    // 属于"假装能用"),等实装注释功能后再回到菜单
    struct { int col; QString name; } cols[] = {
        {SORT_NAME,      gazeTr("文件名")},
        {SORT_EXT,       gazeTr("扩展名")},
        {SORT_MDATE,     gazeTr("修改日期")},
        {SORT_CDATE,     gazeTr("创建日期")},
        {SORT_EXIF,      gazeTr("EXIF 拍摄日期")},
        {SORT_EXIFMOD,   gazeTr("EXIF 修改日期")},
        {SORT_TYPE,      gazeTr("类型")},
        {SORT_SIZE,      gazeTr("文件大小")},
        {SORT_IMGSIZE,   gazeTr("图像大小")},
        {SORT_WIDTH,     gazeTr("图像宽度")},
        {SORT_HEIGHT,    gazeTr("图像高度")},
        {SORT_ORIENTATION, gazeTr("图像方向")},
        {SORT_RATIO,     gazeTr("图像比例")},
        {SORT_PRINTSIZE, gazeTr("打印尺寸")},
        {SORT_PATH,      gazeTr("路径")},
        {SORT_COLORLABEL, gazeTr("颜色标签")},
    };
    for (auto& c : cols) {
        QAction* a = m->addAction(c.name, this, [this, c]() {
            m_fileGrid->sort(c.col, m_fileGrid->sortAscending());
        });
        a->setCheckable(true);
        a->setData(c.col);
    }
    m->addSeparator();
    {
        auto* a = m->addAction(gazeTr("升序"), this, [this]() {
            m_fileGrid->sort(m_fileGrid->currentSortCol(), true);
        });
        a->setCheckable(true); a->setData(1001);
    }
    {
        auto* a = m->addAction(gazeTr("降序"), this, [this]() {
            m_fileGrid->sort(m_fileGrid->currentSortCol(), false);
        });
        a->setCheckable(true); a->setData(1002);
    }
    m->addSeparator();
    // 文件名顺序三式:数字感知(默认,资源管理器风格)/纯字母/系统规则
    {
        auto* a = m->addAction(gazeTr("文件名 - 数字顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameNatural);
        });
        a->setCheckable(true); a->setData(2000 + NameNatural);
    }
    {
        auto* a = m->addAction(gazeTr("文件名 - 字母顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameAlpha);
        });
        a->setCheckable(true); a->setData(2000 + NameAlpha);
    }
    {
        auto* a = m->addAction(gazeTr("文件名 - 正常顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameNormal);
        });
        a->setCheckable(true); a->setData(2000 + NameNormal);
    }
    m->addSeparator();
    {
        auto* a = m->addAction(gazeTr("显示列标题"), this, [this]() {
            m_sortHeader->setVisible(!m_sortHeader->isVisible());
            AppSettings::instance().setPersist("Browser/sortHeader", m_sortHeader->isVisible());
        });
        a->setCheckable(true); a->setData(3000);
    }
    // #107:每次弹出重勾 —— 表头点击排序、外观页改卡宽都不经菜单,勾选必须现算
    connect(m, &QMenu::aboutToShow, this, [m, this]() {
        const int col = m_fileGrid->currentSortCol();
        const bool asc = m_fileGrid->sortAscending();
        const int no = m_fileGrid->nameOrder();
        for (QAction* a : m->actions()) {
            if (!a->isCheckable()) continue;
            const int d = a->data().toInt();
            if (d == 1001)      a->setChecked(asc);
            else if (d == 1002) a->setChecked(!asc);
            else if (d >= 2000 && d < 3000) a->setChecked(d - 2000 == no);
            else if (d == 3000) a->setChecked(m_sortHeader->isVisible());
            else                a->setChecked(d == col);
        }
    });
    m->addSeparator();
    auto* zoomMenu = m->addMenu(gazeTr("缩略图缩放"));
    zoomMenu->addAction(gazeTr("放大"), QKeySequence("Ctrl+="), this, [this](){ onThumbZoom(1); });
    zoomMenu->addAction(gazeTr("缩小"), QKeySequence("Ctrl+-"), this, [this](){ onThumbZoom(-1); });
    return m;
}

QMenu* MainWindow::createFilterMenu(QWidget* parent) {
    auto *m = new QMenu(gazeTr("筛选"), parent);
    bool sepDone = false;
    for (const mw_impl::FilterEntry& it : mw_impl::kFilterModes) {
        const QString name = gazeTr(it.name);
        if (it.mode < 0) {   // 自定义占位
            QAction* a = m->addAction(name);
            a->setEnabled(false);
            continue;
        }
        if (!sepDone && it.mode == FILTER_RED) {
            m->addSeparator();   // 类型组 | 颜色标记组 分隔线
            sepDone = true;
        }
        QAction* a = m->addAction(name);
        a->setCheckable(true);
        a->setData(it.mode);   // aboutToShow 刷新勾选用
        const int mode = it.mode;
        connect(a, &QAction::triggered, this, [this, mode]() {
            // #125:「自定义」不是一个静态档位 —— 选中它就是要编辑自己的清单
            if (mode == FILTER_CUSTOM) { editCustomFilter(); return; }
            m_fileGrid->setFilterMode(mode);
        });
    }
    // 每次弹出刷新勾选状态(反映当前筛选)
    connect(m, &QMenu::aboutToShow, this, [this, m]() {
        int cur = m_fileGrid->filterMode();
        for (QAction* a : m->actions()) {
            if (!a->isCheckable()) continue;
            bool ok = false;
            int mode = a->data().toInt(&ok);
            if (ok) a->setChecked(mode == cur);
        }
    });
    return m;
}

void MainWindow::createStatusbar() {
    auto *sb = statusBar();
    sb->setStyleSheet(QString::fromUtf8(
        "QStatusBar{background:%1;border-top:1px solid %2;"
        "color:%3;font-size:11px;padding:2px 10px;}"
        "QStatusBar::item{border:none;}")
        .arg(C_STATUSBAR, C_SEPARATOR, C_TEXT));
    sb->setFixedHeight(28);
    m_statusLabel = new QLabel;
    m_statusLabel->setStyleSheet(QString("color:%1;background:transparent;").arg(C_TEXT));
    sb->addWidget(m_statusLabel, 1);
    m_pathLabel = new QLabel;
    m_pathLabel->setStyleSheet(QString("color:%1;background:transparent;").arg(C_TEXT));
    sb->addPermanentWidget(m_pathLabel);
}

// ═══════════════════════════════════════════
// 文件页上方工具栏:地址行(导航+路径) + 查看方式/排序/筛选/红标三态/列数
// ═══════════════════════════════════════════
void MainWindow::createToolbar2(QVBoxLayout* intoCenter) {
    const QString barQss =
        QString::fromUtf8("QWidget{background:%1;border-bottom:1px solid %2;}"
        "QToolButton{background:transparent;border:none;border-radius:4px;"
        "padding:3px 6px;color:%3;font-size:11px;}"
        "QToolButton:hover{background:%4;}"
        "QToolButton::menu-indicator{image:none;}").arg(C_TOOLBAR, C_SEPARATOR, C_TEXT, C_CARD_HOVER);

    // ── 地址行:上一级 + 路径输入 + 历史下拉 ──
    auto* addrRow = new QWidget;
    m_addrRow = addrRow;            // 视图菜单可隐藏
    addrRow->setFixedHeight(36);
    addrRow->setStyleSheet(barQss);
    auto* al = new QHBoxLayout(addrRow);
    al->setContentsMargins(6, 4, 6, 4);
    al->setSpacing(4);
    auto mkAddrNav = [&](const QString& icon, const QString& tip, auto slot) {
        auto* btn = new QToolButton;
        btn->setIcon(IconLib::appIcon(icon));
        btn->setIconSize(QSize(17, 17));
        btn->setToolTip(tip);
        btn->setFixedSize(28, 26);
        // 点击后不留焦点:按钮若停在焦点上,随后一次空格 = 再按一次该按钮
        // (上级/前进/后退会被意外触发)。只保留 Tab 可达,同资源管理器工具栏惯例。
        btn->setFocusPolicy(Qt::TabFocus);
        connect(btn, &QToolButton::clicked, this, slot);
        al->addWidget(btn);
    };
    mkAddrNav("up", gazeTr("上级目录 (Backspace)"),
          [this](){ goUp(); });

    m_addrBar = new QLineEdit;
    m_addrBar->setFixedHeight(26);
    m_addrBar->setPlaceholderText(gazeTr("输入路径,回车跳转"));
    m_addrBar->setToolTip(gazeTr(
        "回车跳转到该路径(目录=进去,文件=进它的目录并选中)\n单击全选整条路径,再点一下落光标"));
    m_addrBar->setStyleSheet(QString::fromUtf8(
        "QLineEdit{background:%1;color:%2;"
        "border:1px solid %3;"
        "border-radius:4px;padding:2px 8px;font-size:11px;}")
        .arg(C_CONTENT, C_TEXT, C_CARD_BORDER));
    connect(m_addrBar, &QLineEdit::returnPressed, this, &MainWindow::gotoTypedPath);
    al->addWidget(m_addrBar, 1);

    // 历史路径下拉(上限 30,无动画)
    auto* histBtn = new QToolButton;
    histBtn->setText(gazeTr("▼")); // ▼
    histBtn->setFixedSize(22, 26);
    histBtn->setStyleSheet(QString::fromUtf8("QToolButton{color:%1;font-size:9px;}")
                               .arg(C_SB_ARROW));   // #151:同款小号箭头灰
    histBtn->setToolTip(gazeTr("历史访问路径")); // 历史访问路径
    histBtn->setPopupMode(QToolButton::InstantPopup);
    auto* histMenu = new QMenu(histBtn);
    connect(histMenu, &QMenu::aboutToShow, this, [this, histMenu]() {
        histMenu->clear();
        QSettings s = mw_impl::appSettings();
        QStringList lst = s.value("Browser/pathHistory").toStringList();
        if (lst.isEmpty()) {
            histMenu->addAction(gazeTr("(空)"))->setEnabled(false);
            return;
        }
        for (const auto& p : lst)
            histMenu->addAction(mw_impl::displayPath(p), this, [this, p]() { navigateTo(p); });
    });
    histBtn->setMenu(histMenu);
    al->addWidget(histBtn);
    intoCenter->addWidget(addrRow);

    // ── 功能行:后退/前进/刷新 + 查看方式/排序/筛选/红标三态/列数 ──
    auto* bar2 = new QWidget;
    m_toolRow = bar2;               // 视图菜单可隐藏
    bar2->setFixedHeight(34);
    bar2->setStyleSheet(barQss);
    auto* b2 = new QHBoxLayout(bar2);
    b2->setContentsMargins(6, 3, 6, 3);
    b2->setSpacing(2);

    auto mkNav = [&](const QIcon& ic, const QString& tip, auto slot) {
        auto* btn = new QToolButton;
        btn->setIcon(ic);
        btn->setIconSize(QSize(17, 17));
        btn->setToolTip(tip);
        btn->setFixedSize(28, 26);
        connect(btn, &QToolButton::clicked, this, slot);
        b2->addWidget(btn);
        return btn;
    };
    m_btnBack = mkNav(IconLib::appIcon("cmd_filePrevious"), gazeTr("后退 (Alt+←)"),
                      [this](){ goBack(); });
    m_btnFwd  = mkNav(IconLib::appIcon("cmd_fileNext"), gazeTr("前进 (Alt+→)"),
                      [this](){ goForward(); });
    mkNav(IconLib::appIcon("cmd_refresh"), gazeTr("刷新 (F5)"),
          [this](){ refresh(); });
    // 工具栏可能建在第一跳 navigateTo 之后，这里补一次初始状态
    updateNavEnabled();

    auto mkMenuBtn = [&](const QIcon& ic, const QString& tip, QMenu* menu) {
        auto* btn = new QToolButton;
        btn->setIcon(ic);
        btn->setIconSize(QSize(17, 17));
        btn->setToolTip(tip);
        // #134(复报)：这三颗是"有菜单但没有箭头"的元凶 —— barQss 里
        // QToolButton::menu-indicator{image:none} 把 Qt 自带指示也关了，于是只剩一个
        // 方形图标，肉眼完全看不出能展开(违反「控件须有可见指示器」)。
        // 改成图标旁带一颗 ▼，与地址栏历史按钮(histBtn)同一形态 = 用户点名的参照物。
        btn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        btn->setText(gazeTr("▼")); // ▼
        btn->setFixedSize(46, 26);
        // #151:▼ 原样继承 barQss 的 11px/C_TEXT(近白)= 用户点名"太白太大"。
        // 按钮自有表只压这两项:9px + 箭头灰(C_SB_ARROW,与滚动条/数字框箭头同色);
        // 底色/悬停底仍走 barQss 的 QToolButton 规则(自有表只覆盖冲突属性)
        btn->setStyleSheet(QString::fromUtf8("QToolButton{color:%1;font-size:9px;}")
                               .arg(C_SB_ARROW));
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setMenu(menu);
        b2->addWidget(btn);
    };
    mkMenuBtn(IconLib::appIcon("viewas"),
              gazeTr("查看方式"), createViewModeMenu(bar2));
    mkMenuBtn(IconLib::appIcon("sort"),
              gazeTr("排序"), createSortMenu(bar2));
    mkMenuBtn(IconLib::appIcon("cmd_filter"),
              gazeTr("筛选"), createFilterMenu(bar2));

    // 红色标记三态筛选:显示全部 → 仅红色标记 → 仅未标记
    auto* redBtn = new QToolButton;
    redBtn->setIcon(IconLib::appIcon("cmd_showRed"));
    redBtn->setIconSize(QSize(17, 17));
    redBtn->setFixedSize(30, 26);
    redBtn->setToolTip(gazeTr(
        "筛选红色标记(三态循环):\n"
        "第 1 次点击 — 只显示红色标记的文件\n"
        "第 2 次点击 — 只显示未标红的文件\n"
        "第 3 次点击 — 恢复显示全部\n"
        "(用 Ctrl+1 或 F 给文件加红色标记)"));
    m_redBtn = redBtn;   // #107:背景指示器改由 syncFilterIndicators 独家维护(原先只在 clicked 里设,别处改筛选就脱钩)
    connect(redBtn, &QToolButton::clicked, this, [this]() {
        cycleRedFilter();
    });
    b2->addWidget(redBtn);

    auto* sep2 = new QFrame;
    sep2->setFrameShape(QFrame::VLine);
    sep2->setFixedHeight(18);
    sep2->setStyleSheet("color:#2A2A2E;");
    b2->addWidget(sep2);

    // 缩略图列数:自动 + 1-16(手动指定后缩放窗口时缩略图贴边缩放但列数不变)
    // #134:InstantPopup 按钮的 menu-indicator 被 barQss 关掉,补 ▼ 文本承担"点开有菜单"的可见指示
    auto* colsBtn = new QToolButton;
    colsBtn->setIcon(IconLib::appIcon("cmd_paneThumbs"));
    colsBtn->setIconSize(QSize(17, 17));
    colsBtn->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    colsBtn->setText(gazeTr("▼"));
    colsBtn->setFixedSize(46, 26);
    colsBtn->setStyleSheet(QString::fromUtf8("QToolButton{color:%1;font-size:9px;}")
                               .arg(C_SB_ARROW));   // #151:同款小号箭头灰
    colsBtn->setToolTip(gazeTr(
        "缩略图列数\n手动指定后,拖动边框/缩放窗口时缩略图贴边缩放但列数不变"));
    colsBtn->setPopupMode(QToolButton::InstantPopup);
    auto* colsMenu = new QMenu(colsBtn);
    auto* colsGroup = new QActionGroup(colsMenu);
    colsGroup->setExclusive(true);
    int curFixed = m_fileGrid ? m_fileGrid->fixedCols() : 0;   // 构造期 grid 未创建
    auto* autoAct = colsMenu->addAction(gazeTr("自动"));
    autoAct->setCheckable(true);
    autoAct->setChecked(curFixed == 0);
    colsGroup->addAction(autoAct);
    connect(autoAct, &QAction::triggered, this,
            [this](){ m_fileGrid->setFixedCols(0); });
    colsMenu->addSeparator();
    for (int n = 1; n <= 16; ++n) {
        QAction* a = colsMenu->addAction(gazeTr("%1 列").arg(n));
        a->setCheckable(true);
        a->setChecked(curFixed == n);
        colsGroup->addAction(a);
        connect(a, &QAction::triggered, this,
                [this, n](){ m_fileGrid->setFixedCols(n); });
    }
    colsBtn->setMenu(colsMenu);
    b2->addWidget(colsBtn);

    // 格式筛选:常显当前类型,比开菜单直达。
    // 「格式:」文字标签已按要求去掉,改用一条与 sep2 同规格的竖线独立成组,
    // 避免下拉框紧贴"缩略图列数"按钮;语义由 toolTip + 选项自身文字承担。
    // 注意:这里只列"格式"类,且用的是本框自己的短标签(图片/文档…),
    // 与「筛选」菜单的条目并不是一一对应(菜单另有 图像(+目录)、颜色标记…)。
    // 落在那些模式时框里不再谎报"全部",而是显示"筛选：xxx"(见构造函数里的反向同步)。
    auto* fmtSep = new QFrame;
    fmtSep->setFrameShape(QFrame::VLine);
    fmtSep->setFixedHeight(18);
    fmtSep->setStyleSheet("color:#2A2A2E;");
    b2->addWidget(fmtSep);

    m_formatFilterCombo = new QComboBox;
    m_formatFilterCombo->setFixedSize(112, 26);
    m_formatFilterCombo->setToolTip(gazeTr("按文件格式筛选当前目录"));
    {
        struct { const char* label; int mode; } items[] = {
            {"全部",               FILTER_ALL},          // 全部
            {"图片",               FILTER_IMAGES},       // 图片
            {"视频",               FILTER_VIDEOS},       // 视频
            {"音频",               FILTER_AUDIO},        // 音频
            {"文档",               FILTER_DOCUMENTS},    // 文档
            {"压缩文件", FILTER_ARCHIVES}, // 压缩文件
            {"可执行文件", FILTER_EXECUTABLES}, // 可执行文件
            {"文件夹",   FILTER_FOLDERS},      // 文件夹
            {"自定义…", FILTER_CUSTOM}, // 自定义…(#125)
        };
        for (auto& it : items)
            m_formatFilterCombo->addItem(gazeTr(it.label), it.mode);
    }
    // 下箭头(#151):局部表里那份 CSS 边框三角(#134 二次实测认为能画出来)
    // 用户在真机上仍然看不到 —— 根因未明,不再赌样式表,改用与三颗按钮同款的
    // "▼" 文本:QLabel 叠在 drop-down 区,WA_TransparentForMouseEvents 让点击
    // 穿透回框。框尺寸固定 112x26,几何一次摆放即可,无 resize 问题。
    // ::down-arrow 保留"置零"规则(image:none+0尺寸),防止原生箭头跟 ▼ 重影。
    m_formatFilterCombo->setStyleSheet(QString::fromUtf8(
        "QComboBox{background:%1;color:%2;border:1px solid %3;"
        "border-radius:4px;padding:2px 10px;font-size:12px;min-height:22px;}"
        "QComboBox:hover{border-color:#4A4A56;}"
        "QComboBox:focus{border-color:%4;}"
        "QComboBox::drop-down{width:18px;border:none;background:transparent;"
        "subcontrol-origin:padding;subcontrol-position:top right;}"
        "QComboBox::down-arrow{image:none;width:0;height:0;background:none;border:none;}"
        "QComboBox QAbstractItemView{background:%5;color:%6;"
        "border:1px solid %7;selection-background-color:%8;"
        "outline:none;}"
        "QComboBox QAbstractItemView::item{min-height:24px;padding:2px 8px;}")
        .arg(C_TOOLBAR, C_TEXT, C_SEPARATOR, C_ACCENT,
             C_CONTENT, C_TEXT, C_SEPARATOR, C_ACCENT));
    auto* comboArrow = new QLabel(gazeTr("▼"), m_formatFilterCombo);
    comboArrow->setStyleSheet(QString::fromUtf8(
        "color:%1;background:transparent;font-size:9px;").arg(C_SB_ARROW));
    comboArrow->setAlignment(Qt::AlignCenter);
    comboArrow->setAttribute(Qt::WA_TransparentForMouseEvents);
    comboArrow->setGeometry(112 - 18, 0, 18, 26);
    connect(m_formatFilterCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int idx) {
                const int mode = m_formatFilterCombo->itemData(idx).toInt();
                // #125:「自定义…」= 先编辑自己的扩展名清单再套上去;
                // 取消则把指示器弹回当前生效的筛选(不能留着"自定义"的显示假象)
                if (mode == FILTER_CUSTOM) { editCustomFilter(); return; }
                m_fileGrid->setFilterMode(mode);
            });
    b2->addWidget(m_formatFilterCombo);

    b2->addStretch();
    intoCenter->addWidget(bar2);
}
