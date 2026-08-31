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
#include "iconlib.h"
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
        .arg(C_MENUBAR, C_TEXT, C_SEPARATOR, C_PANE_HDR, C_ACCENT));

    // ── 文件(F) ──
    auto *fileMenu = mb->addMenu("文件(&F)");
    fileMenu->addAction(IconLib::appIcon("cmd_open"), "打开",
        this, [this]() {
            auto paths = m_fileGrid->selectedPaths();
            if (!paths.isEmpty()) openWithSystem(paths.first());
        }, QKeySequence("Ctrl+O"));
    auto *recentMenu = fileMenu->addMenu(IconLib::appIcon("cmd_browse"), "最近的文件");
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        rebuildRecentMenu(recentMenu);
    });
    fileMenu->addAction(IconLib::appIcon("cmd_print"), QString::fromUtf8("打印..."),
        this, [this]() {
            // 有选中打选中,没选中打当前列表全部(和右键"打印"同一口径);
            // 夹在里面的文件夹/视频由 PrintDialog 按扩展名滤掉并如实提示
            QStringList paths = m_fileGrid->selectedPaths();
            if (paths.isEmpty())
                for (int i = 0; i < m_fileGrid->fileCount(); ++i)
                    paths << m_fileGrid->pathOf(i);
            PrintDialog::printImages(this, paths);
        }, QKeySequence("Ctrl+P"));
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("刷新(&R)"), this, [this](){ refresh(); }, QKeySequence("F5"));
    fileMenu->addSeparator();
    fileMenu->addAction(QString::fromUtf8("退出(&X)"), this, &QWidget::close, QKeySequence("Alt+X"));

    // ── 编辑(E) ──
    auto *editMenu = mb->addMenu(QString::fromUtf8("编辑(&E)"));
    editMenu->addAction(IconLib::appIcon("cmd_copyPath"),
        QString::fromUtf8("复制绝对路径"), this, [this]() {
            auto paths = m_fileGrid->selectedPaths();
            if (!paths.isEmpty())
                QApplication::clipboard()->setText(paths.join("\n"));
        }, QKeySequence("Ctrl+Shift+C"));
    // #136:重命名必须是一条**带 shortcut 的 QAction**，不能只在键盘过滤器里加分支 ——
    // 设置→交互→快捷键配置页(settings_pages_input.cpp 的 fillTable)只列"带非空 shortcut
    // 的 QAction"，而 applyShortcuts() 也只按 Shortcuts/<动作文本> 读 ini 覆盖。
    // 挂在过滤器里的 F2 从来进不了那张表，这正是用户要求「写入快捷键配置页」的原因。
    // 助记符用 &R(Rename)：Qt 的助记符只要求**同一菜单内**唯一，编辑菜单里没有别的 &R
    // (「刷新(&R)」在文件菜单，不冲突)。
    editMenu->addAction(IconLib::appIcon("cmd_rename"),
        QString::fromUtf8("重命名(&R)"), this, &MainWindow::renameFocused,
        QKeySequence("F3"));
    // #136:重命名必须是一条**带 shortcut 的 QAction**，不能只在键盘过滤器里加分支 ——
    // 设置→交互→快捷键配置页(settings_pages_input.cpp 的 fillTable)只列"带非空 shortcut
    // 的 QAction"，而 applyShortcuts() 也只按 Shortcuts/<动作文本> 读 ini 覆盖。
    // 挂在过滤器里的 F2 从来进不了那张表，这正是用户要求「写入快捷键配置页」的原因。
    // 助记符用 &R(Rename)：Qt 的助记符只要求**同一菜单内**唯一，编辑菜单里没有别的 &R
    // (「刷新(&R)」在文件菜单，不冲突)。
    editMenu->addAction(IconLib::appIcon("cmd_rename"),
        QString::fromUtf8("重命名(&R)"), this, &MainWindow::renameFocused,
        QKeySequence("F3"));
    editMenu->addSeparator();
    editMenu->addAction(IconLib::appIcon("cmd_selectAllFile"),
        QString::fromUtf8("全选"), this, [this](){ m_fileGrid->selectAllEntries(); }, QKeySequence("Ctrl+A"));
    editMenu->addAction(QString::fromUtf8("反选"), this, [this](){ m_fileGrid->selectInvert(); }, QKeySequence("Ctrl+I"));
    editMenu->addSeparator();
    editMenu->addAction(QString::fromUtf8("全选文件"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindFiles); });
    editMenu->addAction(QString::fromUtf8("全选文件夹"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindDirs); });
    editMenu->addAction(QString::fromUtf8("全选图像"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindImages); });
    editMenu->addAction(QString::fromUtf8("全选视频"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindVideos); });
    editMenu->addAction(QString::fromUtf8("全选音频"), this, [this](){ m_fileGrid->selectByKind(FileGrid::KindAudio); });
    // 颜色标签:原"元数据"一级菜单下,按需求挪到编辑(元数据菜单随之删除)
    editMenu->addSeparator();
    auto *labelMenu = editMenu->addMenu(IconLib::appIcon("label_item"),
                                        QString::fromUtf8("设置颜色标签"));
    struct { int c; QString name; } colors[] = {
        {1, QString::fromUtf8("红色  (Ctrl+1)")},
        {2, QString::fromUtf8("橙色  (Ctrl+2)")},
        {3, QString::fromUtf8("黄色  (Ctrl+3)")},
        {4, QString::fromUtf8("绿色  (Ctrl+4)")},
        {5, QString::fromUtf8("蓝色  (Ctrl+5)")},
    };
    for (auto& c : colors)
        labelMenu->addAction(c.name, this, [this, c](){ applyColorLabel(c.c); });
    labelMenu->addSeparator();
    labelMenu->addAction(QString::fromUtf8("取消颜色标记  (Ctrl+0 / D)"), this, [this](){ applyColorLabel(0); });

    // ── 查看(V) ──
    auto *viewMenu = mb->addMenu(QString::fromUtf8("查看(&V)"));
    auto* fsAct = viewMenu->addAction(IconLib::appIcon("cmd_fullscreen"),
        QString::fromUtf8("全屏"), QKeySequence("F11"), this, [this]() {
            if (m_fullView) { exitFullView(); return; }   // F11 也得把全屏查看整个退干净(#154)
            if (isFullScreen()) showNormal(); else enterFullscreen();
        });
    fsAct->setCheckable(true);
    // 键位写在标题里而不挂 QAction::setShortcut:菜单裸键会连文本框里的 G 一起吞掉(#61),
    // 实际响应在 qApp 事件过滤器里(那里有"这个键是不是该给文本框"的判断)
    auto* fullAct = viewMenu->addAction(QString::fromUtf8("全屏查看  (G)"), this, [this]() {
        toggleFullView();
    });
    fullAct->setCheckable(true);
    // #154:全屏=窗口铺满(浏览器态),全屏预览=只铺画面 —— 弹出瞬间按实态重勾
    connect(viewMenu, &QMenu::aboutToShow, this, [this, fsAct, fullAct]() {
        fsAct->setChecked(isFullScreen() && !m_fullView);
        fullAct->setChecked(m_fullView);
    });
    viewMenu->addSeparator();
    viewMenu->addMenu(createViewModeMenu(viewMenu))->setIcon(IconLib::appIcon("viewas"));
    viewMenu->addMenu(createSortMenu(viewMenu))->setIcon(IconLib::appIcon("sort"));
    viewMenu->addMenu(createFilterMenu(viewMenu))->setIcon(IconLib::appIcon("cmd_filter"));
    auto *thumbSizeMenu = viewMenu->addMenu(IconLib::appIcon("cmd_paneThumbs"),
                                            QString::fromUtf8("缩略图尺寸"));
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
        auto* a = thumbSizeMenu->addAction(QString::fromUtf8("自定义..."), this, [this]() {
            bool ok = false;
            AppSettings& st = AppSettings::instance();
            int def = qBound(THUMB_W_MIN, st.get("Appearance/customThumbW", 96).toInt(), THUMB_W_MAX);
            int v = QInputDialog::getInt(this, QString::fromUtf8("自定义缩略图尺寸"),
                QString::fromUtf8("宽度(像素):"), def, THUMB_W_MIN, THUMB_W_MAX, 8, &ok);
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
    viewMenu->addSeparator();
    viewMenu->addAction(QString::fromUtf8("放大缩略图"), this, [this](){ onThumbZoom(1); }, QKeySequence("Ctrl+="));
    viewMenu->addAction(QString::fromUtf8("缩小缩略图"), this, [this](){ onThumbZoom(-1); }, QKeySequence("Ctrl+-"));
    m_actBack = viewMenu->addAction(QString::fromUtf8("后退"), this, [this](){ goBack(); }, QKeySequence("Alt+Left"));
    m_actFwd  = viewMenu->addAction(QString::fromUtf8("前进"), this, [this](){ goForward(); }, QKeySequence("Alt+Right"));
    viewMenu->addAction(QString::fromUtf8("上级目录"), this, [this](){ navigateTo(".."); }, QKeySequence("Backspace"));

    // ── 布局(L) → 视图 ──(依次追加,顺序即"文件 编辑 查看 布局 视图 工具 帮助")
    createLayoutMenu();
    createViewMenu();

    // ── 工具(T) ──
    // #132:顺序按"先配置工具、再用工具"排 —— 设置 在 以文搜图 上面(用户令)。
    auto *toolMenu = mb->addMenu(QString::fromUtf8("工具(&T)"));
    toolMenu->addAction(IconLib::appIcon("cmd_options"),
        QString::fromUtf8("设置..."), this, [this]() {
            SettingsDialog dlg(this);
            dlg.exec();
        }, QKeySequence("F12"));
    toolMenu->addAction(IconLib::appIcon("cmd_search"),
        QString::fromUtf8("以文搜图..."), this, [this]() {
            ImageSearchDialog dlg(this);
            dlg.exec();
        }, QKeySequence("Ctrl+Shift+F"));
    toolMenu->addAction(IconLib::appIcon("cmd_editMetadata"),
        QString::fromUtf8("缩略图数据库维护..."), this, [this]() {
            DbMaintenanceDialog dlg(this);
            dlg.exec();
        });
    // #123:原「批量重命名...」菜单项已删 —— 该功能在 TODO_ALL §9 否决清单(@153611)。

    // ── 帮助(H) ──
    auto *helpMenu = mb->addMenu("帮助(&H)");
    helpMenu->addAction("快捷键帮助(&K)", this, [](){
        QMessageBox::information(nullptr, "快捷键帮助",
            "C / ← / ↑ — 上一个\n"
            "V / → / ↓ — 下一个\n"
            "空格 — 播放/暂停\n"
            "Ctrl+PgUp/PgDn — 快退/快进(秒数见设置→键盘)\n"
            "按住右键+滚轮 — 缩放(等同 Ctrl+滚轮)\n"
            "双击预览区 — 全屏\n"
            "F5 — 刷新  F11 — 全屏\n"
            "G — 全屏预览(只铺画面;再按 G 或 ESC 完全回到原布局)\n"
            "Alt+←/→ — 后退/前进  Backspace — 上级\n"
            "Ctrl+1~5 — 设置颜色标记(红橙黄绿蓝)\n"
            "Ctrl+0 / D — 取消颜色标记\n"
            "F — 加红色标记\n"
            "F2 / F3 — 重命名(文件树与文件页都可用,改谁看焦点;F3 可在设置→快捷键改)\n"
            "Del / S — 删除选中  X — 新建文件夹\n"
            "Enter — 切换查看器/浏览器(设置→键盘)\n"
            "Ctrl+A — 全选  Ctrl+I — 反选\n"
            "Esc — 退出全屏\n"
            "拖放 — 移动到文件夹  Ctrl+拖放 — 复制(设置→文件操作可关确认弹窗)");
    });
    helpMenu->addAction("关于(&A)", this, [](){
        QMessageBox::about(nullptr, "关于 Gaze",
            "Gaze\n通用图片/文件资源管理器\n\n"
            "主要功能:\n"
            "· 图库浏览(文件夹树 + 缩略图网格 + 预览面板)\n"
            "· Live Photo / Motion Photo 动态照片自动播放\n"
            "· 图片/视频/音频预览,颜色标记与筛选\n"
            "· 图片查看器模式(Ctrl+滚轮缩放细节)\n\n"
            "版本 1.0 — C++ + Qt6");
    });
}

// ═══════════════════════════════════════════
// 查看方式/排序/筛选 菜单(菜单栏与工具栏共用)
// ═══════════════════════════════════════════
QMenu* MainWindow::createViewModeMenu(QWidget* parent) {
    auto *m = new QMenu(QString::fromUtf8("查看方式"), parent);
    // 查看方式 7 种(批次 3 实装差异渲染;当前统一缩略图)
    QStringList modes = {
        QString::fromUtf8("缩略图"), QString::fromUtf8("缩略图 + 文件名"),
        QString::fromUtf8("缩略图 + 标签"), QString::fromUtf8("缩略图 + 详细"),
        QString::fromUtf8("图标"), QString::fromUtf8("列表"),
        QString::fromUtf8("详细信息"), QString::fromUtf8("瀑布流"),
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
    auto *m = new QMenu(QString::fromUtf8("排序"), parent);
    // 注释/自定义排序未上桌:没有对应数据模型(此前静默退化成文件名排序,
    // 属于"假装能用"),等实装注释功能后再回到菜单
    struct { int col; QString name; } cols[] = {
        {SORT_NAME,      QString::fromUtf8("文件名")},
        {SORT_EXT,       QString::fromUtf8("扩展名")},
        {SORT_MDATE,     QString::fromUtf8("修改日期")},
        {SORT_CDATE,     QString::fromUtf8("创建日期")},
        {SORT_EXIF,      QString::fromUtf8("EXIF 拍摄日期")},
        {SORT_EXIFMOD,   QString::fromUtf8("EXIF 修改日期")},
        {SORT_TYPE,      QString::fromUtf8("类型")},
        {SORT_SIZE,      QString::fromUtf8("文件大小")},
        {SORT_IMGSIZE,   QString::fromUtf8("图像大小")},
        {SORT_WIDTH,     QString::fromUtf8("图像宽度")},
        {SORT_HEIGHT,    QString::fromUtf8("图像高度")},
        {SORT_ORIENTATION, QString::fromUtf8("图像方向")},
        {SORT_RATIO,     QString::fromUtf8("图像比例")},
        {SORT_PRINTSIZE, QString::fromUtf8("打印尺寸")},
        {SORT_PATH,      QString::fromUtf8("路径")},
        {SORT_COLORLABEL, QString::fromUtf8("颜色标签")},
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
        auto* a = m->addAction(QString::fromUtf8("升序"), this, [this]() {
            m_fileGrid->sort(m_fileGrid->currentSortCol(), true);
        });
        a->setCheckable(true); a->setData(1001);
    }
    {
        auto* a = m->addAction(QString::fromUtf8("降序"), this, [this]() {
            m_fileGrid->sort(m_fileGrid->currentSortCol(), false);
        });
        a->setCheckable(true); a->setData(1002);
    }
    m->addSeparator();
    // 文件名顺序三式:数字感知(默认,资源管理器风格)/纯字母/系统规则
    {
        auto* a = m->addAction(QString::fromUtf8("文件名 - 数字顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameNatural);
        });
        a->setCheckable(true); a->setData(2000 + NameNatural);
    }
    {
        auto* a = m->addAction(QString::fromUtf8("文件名 - 字母顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameAlpha);
        });
        a->setCheckable(true); a->setData(2000 + NameAlpha);
    }
    {
        auto* a = m->addAction(QString::fromUtf8("文件名 - 正常顺序"), this, [this]() {
            m_fileGrid->setNameOrder(NameNormal);
        });
        a->setCheckable(true); a->setData(2000 + NameNormal);
    }
    m->addSeparator();
    {
        auto* a = m->addAction(QString::fromUtf8("显示列标题"), this, [this]() {
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
    auto* zoomMenu = m->addMenu(QString::fromUtf8("缩略图缩放"));
    zoomMenu->addAction(QString::fromUtf8("放大"), this, [this](){ onThumbZoom(1); }, QKeySequence("Ctrl+="));
    zoomMenu->addAction(QString::fromUtf8("缩小"), this, [this](){ onThumbZoom(-1); }, QKeySequence("Ctrl+-"));
    return m;
}

QMenu* MainWindow::createFilterMenu(QWidget* parent) {
    auto *m = new QMenu(QString::fromUtf8("筛选"), parent);
    bool sepDone = false;
    for (const mw_impl::FilterEntry& it : mw_impl::kFilterModes) {
        const QString name = QString::fromUtf8(it.name);
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
        "QToolButton::menu-indicator{image:none;}").arg(C_TOOLBAR, C_SEPARATOR, C_TEXT, C_PANE_HDR);

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
    mkAddrNav("up", QString::fromUtf8("上级目录 (Backspace)"),
          [this](){ navigateTo(".."); });

    m_addrBar = new QLineEdit;
    m_addrBar->setFixedHeight(26);
    m_addrBar->setPlaceholderText(QString::fromUtf8("输入路径,回车跳转"));
    m_addrBar->setToolTip(QString::fromUtf8(
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
    histBtn->setText(QString::fromUtf8("\xe2\x96\xbc")); // ▼
    histBtn->setFixedSize(22, 26);
    histBtn->setToolTip(QString::fromUtf8("\xe5\x8e\x86\xe5\x8f\xb2\xe8\xae\xbf\xe9\x97\xae\xe8\xb7\xaf\xe5\xbe\x84")); // 历史访问路径
    histBtn->setPopupMode(QToolButton::InstantPopup);
    auto* histMenu = new QMenu(histBtn);
    connect(histMenu, &QMenu::aboutToShow, this, [this, histMenu]() {
        histMenu->clear();
        QSettings s = mw_impl::appSettings();
        QStringList lst = s.value("Browser/pathHistory").toStringList();
        if (lst.isEmpty()) {
            histMenu->addAction(QString::fromUtf8("(空)"))->setEnabled(false);
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
    m_btnBack = mkNav(IconLib::appIcon("cmd_filePrevious"), QString::fromUtf8("后退 (Alt+←)"),
                      [this](){ goBack(); });
    m_btnFwd  = mkNav(IconLib::appIcon("cmd_fileNext"), QString::fromUtf8("前进 (Alt+→)"),
                      [this](){ goForward(); });
    mkNav(IconLib::appIcon("cmd_refresh"), QString::fromUtf8("刷新 (F5)"),
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
        btn->setText(QString::fromUtf8("\xe2\x96\xbc")); // ▼
        btn->setFixedSize(46, 26);
        btn->setPopupMode(QToolButton::InstantPopup);
        btn->setMenu(menu);
        b2->addWidget(btn);
    };
    mkMenuBtn(IconLib::appIcon("viewas"),
              QString::fromUtf8("查看方式"), createViewModeMenu(bar2));
    mkMenuBtn(IconLib::appIcon("sort"),
              QString::fromUtf8("排序"), createSortMenu(bar2));
    mkMenuBtn(IconLib::appIcon("cmd_filter"),
              QString::fromUtf8("筛选"), createFilterMenu(bar2));

    // 红色标记三态筛选:显示全部 → 仅红色标记 → 仅未标记
    auto* redBtn = new QToolButton;
    redBtn->setIcon(IconLib::appIcon("cmd_showRed"));
    redBtn->setIconSize(QSize(17, 17));
    redBtn->setFixedSize(30, 26);
    redBtn->setToolTip(QString::fromUtf8(
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
    auto* colsBtn = new QToolButton;
    colsBtn->setIcon(IconLib::appIcon("cmd_paneThumbs"));
    colsBtn->setIconSize(QSize(17, 17));
    colsBtn->setFixedSize(34, 26);
    colsBtn->setToolTip(QString::fromUtf8(
        "缩略图列数\n手动指定后,拖动边框/缩放窗口时缩略图贴边缩放但列数不变"));
    colsBtn->setPopupMode(QToolButton::InstantPopup);
    auto* colsMenu = new QMenu(colsBtn);
    auto* colsGroup = new QActionGroup(colsMenu);
    colsGroup->setExclusive(true);
    int curFixed = m_fileGrid ? m_fileGrid->fixedCols() : 0;   // 构造期 grid 未创建
    auto* autoAct = colsMenu->addAction(QString::fromUtf8("自动"));
    autoAct->setCheckable(true);
    autoAct->setChecked(curFixed == 0);
    colsGroup->addAction(autoAct);
    connect(autoAct, &QAction::triggered, this,
            [this](){ m_fileGrid->setFixedCols(0); });
    colsMenu->addSeparator();
    for (int n = 1; n <= 16; ++n) {
        QAction* a = colsMenu->addAction(QString::fromUtf8("%1 列").arg(n));
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
    m_formatFilterCombo->setToolTip(QString::fromUtf8("按文件格式筛选当前目录"));
    {
        struct { const char* label; int mode; } items[] = {
            {"\xe5\x85\xa8\xe9\x83\xa8",               FILTER_ALL},          // 全部
            {"\xe5\x9b\xbe\xe7\x89\x87",               FILTER_IMAGES},       // 图片
            {"\xe8\xa7\x86\xe9\xa2\x91",               FILTER_VIDEOS},       // 视频
            {"\xe9\x9f\xb3\xe9\xa2\x91",               FILTER_AUDIO},        // 音频
            {"\xe6\x96\x87\xe6\xa1\xa3",               FILTER_DOCUMENTS},    // 文档
            {"\xe5\x8e\x8b\xe7\xbc\xa9\xe6\x96\x87\xe4\xbb\xb6", FILTER_ARCHIVES}, // 压缩文件
            {"\xe5\x8f\xaf\xe6\x89\xa7\xe8\xa1\x8c\xe6\x96\x87\xe4\xbb\xb6", FILTER_EXECUTABLES}, // 可执行文件
            {"\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9",   FILTER_FOLDERS},      // 文件夹
            {"\xe8\x87\xaa\xe5\xae\x9a\xe4\xb9\x89\xe2\x80\xa6", FILTER_CUSTOM}, // 自定义…(#125)
        };
        for (auto& it : items)
            m_formatFilterCombo->addItem(QString::fromUtf8(it.label), it.mode);
    }
    // 下拉箭头(drop-down/三角)由应用级 QSS 统一给(theme.cpp)—— 这里只留这只框
    // 自己的尺寸与弹出列表配色,不再重复写一份箭头样式(#134 之前两份并存)
    m_formatFilterCombo->setStyleSheet(QString::fromUtf8(
        "QComboBox{background:%1;color:%2;border:1px solid %3;"
        "border-radius:4px;padding:2px 10px;font-size:12px;min-height:22px;}"
        "QComboBox:hover{border-color:#4A4A56;}"
        "QComboBox:focus{border-color:%4;}"
        "QComboBox QAbstractItemView{background:%5;color:%6;"
        "border:1px solid %7;selection-background-color:%8;"
        "outline:none;}"
        "QComboBox QAbstractItemView::item{min-height:24px;padding:2px 8px;}")
        .arg(C_TOOLBAR, C_TEXT, C_SEPARATOR, C_ACCENT,
             C_CONTENT, C_TEXT, C_SEPARATOR, C_ACCENT));
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
