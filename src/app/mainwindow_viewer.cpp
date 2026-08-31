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

// ═══════════════════════════════════════════
// 布局方案:保存/应用窗口几何与分栏宽度
// ═══════════════════════════════════════════
// 可落盘的分栏宽度。查看器模式下树/网格被藏起来,实时 sizes() 是 "0,0,W",
// 直接写回会把"只是进了查看器"永久存成"用户把网格拖成了 0":
// splitterArchiveUsable 判定网格=0 的存档不可信,下次启动布局再也回不来。
// 与 panes 同一原则——存"浏览器意图",不存控件瞬时状态。
QString MainWindow::splitterCsv() const {
    const QList<int> sz = (m_viewerMode && m_savedSplitter.size() == 3)
                        ? m_savedSplitter : m_splitter->sizes();
    QStringList out;
    for (int v : sz) out << QString::number(v);
    return out.join(',');
}

void MainWindow::saveLayout(const QString& name) {
    QSettings s = mw_impl::appSettings();
    s.setValue("Layout/" + name + "/geometry", saveGeometry().toHex());
    s.setValue("Layout/" + name + "/splitter", splitterCsv());
    s.setValue("Layout/" + name + "/panes", m_panesOn.join(','));
    QStringList names = s.value("Layout/names").toStringList();
    if (!names.contains(name)) { names.append(name); s.setValue("Layout/names", names); }
    s.setValue("Layout/active", name);
    s.setValue("Layout/followLast", false);   // 应用/保存命名布局后不再跟随上次
}

void MainWindow::applyLayout(const QString& name) {
    QSettings s = mw_impl::appSettings();
    QByteArray geo = QByteArray::fromHex(
        s.value("Layout/" + name + "/geometry").toByteArray());
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QList<int> sz = mw_impl::parseSplitterSizes(
        s.value("Layout/" + name + "/splitter").toString());
    if (mw_impl::splitterArchiveUsable(sz)) m_splitter->setSizes(sz);
    restorePanes(s.value("Layout/" + name + "/panes").toString());
    s.setValue("Layout/active", name);
    s.setValue("Layout/followLast", false);
}

void MainWindow::applyLastLayout() {
    QSettings s = mw_impl::appSettings();
    QByteArray geo = QByteArray::fromHex(s.value("Layout/last/geometry").toByteArray());
    if (!geo.isEmpty()) restoreGeometry(geo);
    const QList<int> sz = mw_impl::parseSplitterSizes(
        s.value("Layout/last/splitter").toString());
    if (mw_impl::splitterArchiveUsable(sz)) m_splitter->setSizes(sz);
    restorePanes(s.value("Layout/last/panes").toString());
}

void MainWindow::createLayoutMenu() {
    auto* lm = new QMenu(QString::fromUtf8("布局(&L)"), this);
    lm->setStyleSheet(menuBar()->styleSheet());
    menuBar()->addMenu(lm);   // 追加到末尾:菜单顺序由 createMenubar 的调用顺序决定
    connect(lm, &QMenu::aboutToShow, this, [this, lm]() {
        lm->clear();
        QSettings s = mw_impl::appSettings();
        bool followLast = s.value("Layout/followLast", true).toBool();
        QAction* follow = lm->addAction(QString::fromUtf8("跟随上次窗口状态"));
        follow->setCheckable(true);
        follow->setChecked(followLast);
        connect(follow, &QAction::triggered, this, [this, follow]() {
            QSettings st = mw_impl::appSettings();
            st.setValue("Layout/followLast", follow->isChecked());
            if (!follow->isChecked()) {   // 取消跟随:以当前状态落一个"上次"
                st.setValue("Layout/last/geometry", saveGeometry().toHex());
                st.setValue("Layout/last/splitter", splitterCsv());
            }
        });
        lm->addSeparator();
        lm->addAction(QString::fromUtf8("保存当前布局..."), this, [this]() {
            QSettings st = mw_impl::appSettings();
            QStringList names = st.value("Layout/names").toStringList();
            QString def = QString::fromUtf8("布局 %1").arg(names.size() + 1);
            QString name = QInputDialog::getText(this,
                QString::fromUtf8("保存当前布局"),
                QString::fromUtf8("布局名称:"), QLineEdit::Normal, def).trimmed();
            if (name.isEmpty()) return;
            // 名字直接拼进 QSettings 键(Layout/<name>/geometry):
            // 含斜杠会写进别的组,叫 last/active/names 则撞上内置状态键并毁掉存档
            if (const QString why = invalidNameReason(name); !why.isEmpty()) {
                QMessageBox::warning(this, QString::fromUtf8("保存当前布局"), why);
                return;
            }
            const QString lc = name.toLower();
            if (lc == QLatin1String("last") || lc == QLatin1String("active")
                || lc == QLatin1String("followlast") || lc == QLatin1String("names")) {
                QMessageBox::warning(this, QString::fromUtf8("保存当前布局"),
                    QString::fromUtf8("“%1”是布局存档的保留名称，换一个").arg(name));
                return;
            }
            saveLayout(name);
        });
        QStringList names = s.value("Layout/names").toStringList();
        if (!names.isEmpty()) {
            lm->addSeparator();
            QString active = s.value("Layout/active").toString();
            for (const auto& n : names) {
                QAction* a = lm->addAction(n);
                a->setCheckable(true);
                a->setChecked(n == active && !followLast);
                connect(a, &QAction::triggered, this, [this, n]() {
                    applyLayout(n);
                });
            }
            lm->addSeparator();
            lm->addAction(QString::fromUtf8("删除布局..."), this, [this, lm]() {
                QSettings st = mw_impl::appSettings();
                QStringList ns = st.value("Layout/names").toStringList();
                if (ns.isEmpty()) return;
                bool ok = false;
                QString del = QInputDialog::getItem(this,
                    QString::fromUtf8("删除布局"),
                    QString::fromUtf8("选择要删除的布局:"), ns, 0, false, &ok);
                if (!ok || del.isEmpty()) return;
                st.remove("Layout/" + del);
                ns.removeAll(del);
                st.setValue("Layout/names", ns);
                if (st.value("Layout/active").toString() == del)
                    st.remove("Layout/active");   // 不留孤儿 active(下次启动跟随上次)
            });
        }
    });
}

// ═══════════════════════════════════════════
// 面板显隐(视图菜单 + 标题条 X)
//   m_panesOn 只记"用户意图":查看器模式对树/网格的临时隐藏不改它,
//   否则进一次查看器就会被误存成"用户关掉了文件列表"
// ═══════════════════════════════════════════
// "info" = #80 元数据面板+直方图,默认不打开(见 kPanesDefault)
static const char* const kPanes[] = { "tree", "preview", "addr", "tool", "status", "info" };
static const int kPaneCount = int(sizeof(kPanes) / sizeof(kPanes[0]));

QStringList MainWindow::paneIds() const {
    QStringList out;
    for (int i = 0; i < kPaneCount; ++i) out << QString::fromLatin1(kPanes[i]);
    return out;
}

bool MainWindow::paneOn(const QString& id) const {
    return m_panesOn.contains(id);
}

bool MainWindow::paneVisible(const char* paneId) const {
    return paneOn(QString::fromLatin1(paneId));
}

// XnView 式面板标题条:左标题 + 右关闭 X
QWidget* MainWindow::createPaneHeader(const QString& title, const char* paneId) {
    auto* h = new QWidget;
    h->setFixedHeight(24);
    h->setStyleSheet(QString::fromUtf8(
        "QWidget{background:%1;border-bottom:1px solid %2;}"
        "QToolButton{background:transparent;border:none;border-radius:4px;"
        "color:%3;font-size:13px;}"
        "QToolButton:hover{background:%4;}")
        .arg(C_PANE_HDR, C_SEPARATOR, C_TEXT, C_MENUBAR));
    auto* hl = new QHBoxLayout(h);
    hl->setContentsMargins(8, 0, 3, 0);
    hl->setSpacing(0);
    auto* lbl = new QLabel(title);
    // 不用 600 字重:雅黑只有 400/700 两档真字重,600 会被就近硬凑,
    // 12px 小字上笔画发虚;全应用其余文字均为常规字重且清晰
    lbl->setStyleSheet(QString::fromUtf8("background:transparent;color:%1;font-size:12px;").arg(C_TEXT));
    hl->addWidget(lbl, 1);
    auto* x = new QToolButton;
    x->setText(QString::fromUtf8("\xc3\x97"));   // ×
    x->setFixedSize(18, 18);
    x->setCursor(Qt::PointingHandCursor);
    x->setToolTip(QString::fromUtf8("隐藏此面板(视图菜单可再打开)"));
    connect(x, &QToolButton::clicked, this, [this, paneId]() {
        setPaneVisible(paneId, false);
    });
    hl->addWidget(x);
    return h;
}

// 意图 + 查看器模式 → 实际可见性(唯一出口,别处不要直接 setVisible 面板)
void MainWindow::applyPaneVisibility() {
    if (m_treePane)    m_treePane->setVisible(paneOn("tree") && !m_viewerMode);
    if (m_centerPane)  m_centerPane->setVisible(!m_viewerMode);
    if (m_previewPane) m_previewPane->setVisible(paneOn("preview") || m_viewerMode);
    if (m_previewHdr)  m_previewHdr->setVisible(!m_viewerMode);
    if (m_addrRow)     m_addrRow->setVisible(paneOn("addr"));
    if (m_toolRow)     m_toolRow->setVisible(paneOn("tool"));
    statusBar()->setVisible(paneOn("status"));
    if (m_infoPane) {
        const bool on = paneOn("info");
        m_infoPane->setVisible(on);
        // 打开即记"见过":下次启动不再强制剔除
        if (on && !AppSettings::instance().get("Interface/infoPanelSeen", false).toBool())
            AppSettings::instance().set("Interface/infoPanelSeen", true);
    }
}

void MainWindow::setPaneVisible(const char* paneId, bool on, bool remember) {
    const QString id = QString::fromLatin1(paneId);
    if (remember) {
        if (on && !m_panesOn.contains(id)) m_panesOn.append(id);
        else if (!on)                      m_panesOn.removeAll(id);
        // 按 kPanes 顺序归一:落盘字符串稳定,且与 m_paneActs 下标对齐
        QStringList ordered;
        for (const QString& k : paneIds()) if (m_panesOn.contains(k)) ordered << k;
        m_panesOn = ordered;
    }
    applyPaneVisibility();
    for (int i = 0; i < kPaneCount; ++i)
        if (m_paneActs[i] && QString::fromLatin1(kPanes[i]) == id)
            m_paneActs[i]->setChecked(on);
}

// csv = "tree,preview,..";空(从未保存过)= 全部显示,复现旧版始终可见的行为
void MainWindow::restorePanes(const QString& csv) {
    const QStringList all = paneIds();
    if (csv.trimmed().isEmpty()) {
        m_panesOn = all;
    } else {
        const QStringList want = csv.split(',', Qt::SkipEmptyParts);
        QStringList on;
        for (const QString& k : all) if (want.contains(k)) on << k;
        m_panesOn = on.isEmpty() ? all : on;
    }
    applyPaneVisibility();
    for (int i = 0; i < kPaneCount; ++i)
        if (m_paneActs[i]) m_paneActs[i]->setChecked(m_panesOn.contains(all[i]));
}

// ── 一级菜单"视图":面板开关,开着的显示 ✓ ──
void MainWindow::createViewMenu() {
    auto* vm = new QMenu(QString::fromUtf8("视图(&W)"), this);
    vm->setStyleSheet(menuBar()->styleSheet());
    menuBar()->addMenu(vm);

    struct Item { const char* id; const char* name; const char* key; };
    const Item items[] = {
        { "tree",    "\xe6\x96\x87\xe4\xbb\xb6\xe5\xa4\xb9\xe6\xa0\x91", "" },      // 文件夹树
        // #136:用户令「F3 不是预览的快捷键，而是重命名的快捷键」→ F3 让给重命名。
        // **这一行不能删**：kPanes/m_paneActs/items 是三张并行表，数量由下面的
        // static_assert 钉死，删行=编译不过 + 按下标写 m_paneActs 会错位。
        // 连带影响（如实记录）：预览面板从此没有默认快捷键，而设置→快捷键配置页
        // 只列"带 shortcut 的动作"，所以它不再出现在那张表里。
        { "preview", "\xe9\xa2\x84\xe8\xa7\x88\xe9\x9d\xa2\xe6\x9d\xbf", "" },     // 预览面板
        { "addr",    "\xe5\x9c\xb0\xe5\x9d\x80\xe6\xa0\x8f", "" },                 // 地址栏
        { "tool",    "\xe5\xb7\xa5\xe5\x85\xb7\xe6\xa0\x8f", "" },                 // 工具栏
        { "status",  "\xe7\x8a\xb6\xe6\x80\x81\xe6\xa0\x8f", "" },                 // 状态栏
        { "info",    "\xe4\xbf\xa1\xe6\x81\xaf\xe9\x9d\xa2\xe6\x9d\xbf", "F9" },     // 信息面板(元数据+直方图)
    };
    const QStringList all = paneIds();
    // kPanes / m_paneActs / items 是三张手工并行维护的表。飘了的后果不是"菜单
    // 少一项"而是越界写:items[].id 与 kPanes 不同名时 all.indexOf() 返回 -1,
    // 旧代码照写 m_paneActs[-1] = a,直接改坏对象内存(面板勾选从此错乱)。
    // 数量在编译期钉死,名字在运行期兜住。
    static_assert(int(sizeof(items) / sizeof(items[0])) == kPaneCount,
                  "视图菜单条目必须与 kPanes 一一对应");
    static_assert(int(sizeof(m_paneActs) / sizeof(m_paneActs[0])) >= kPaneCount,
                  "m_paneActs 容量必须覆盖 kPanes");
    for (int i = 0; i < kPaneCount; ++i) {
        QAction* a = vm->addAction(QString::fromUtf8(items[i].name));
        a->setCheckable(true);
        a->setChecked(paneOn(QString::fromLatin1(items[i].id)));
        if (items[i].key[0]) a->setShortcut(QKeySequence(items[i].key));
        const char* id = items[i].id;   // 指向静态字面量,可安全捕获
        connect(a, &QAction::triggered, this, [this, id]() {
            setPaneVisible(id, !paneVisible(id));
        });
        const int row = all.indexOf(QString::fromLatin1(items[i].id));
        Q_ASSERT(row >= 0);             // 数量对得上却查不到 = 两张表名字飘了
        if (row >= 0) m_paneActs[row] = a;
    }
    // ✓ 每次展开按意图重算:启动恢复布局/程序化改显隐都不会让勾漂移
    connect(vm, &QMenu::aboutToShow, this, [this, vm, all]() {
        for (int i = 0; i < kPaneCount; ++i)
            if (m_paneActs[i]) m_paneActs[i]->setChecked(m_panesOn.contains(all[i]));
    });
}

// ── 浏览器 ↔ 查看器(单图模式:隐藏树/网格,预览占满) ──
void MainWindow::toggleViewer() {
    m_viewerMode = !m_viewerMode;
    Logger::event(QStringLiteral("toggleViewer -> %1")
                      .arg(m_viewerMode ? QStringLiteral("viewer")
                                        : QStringLiteral("browser")));   // #128② 取证
    if (!m_viewerMode && m_slideshow) toggleSlideshow();   // 退出查看器停幻灯片
    applyPaneVisibility();             // 树/网格/预览标题条统一按"意图+模式"重算
    m_preview->setViewerMode(m_viewerMode);   // 让面板按 Viewer/* 还是 Fullscreen/* 取设置
    if (m_viewerMode) {
        m_preview->setMinimumWidth(400);
        m_savedSplitter = m_splitter->sizes();   // 记住进入前布局(拖过的分栏不丢)
        QList<int> sz { 0, 0, width() };
        m_splitter->setSizes(sz);
        // 进查看器:标签表跨退出保留,先丢掉文件已经不在的那几张(在浏览器里删过的),
        // 再按当前文件补开/就地同步。#105:索引 0 的「浏览器」标签常驻,点它
        // 回标准模式,所以进查看器后标签条必有内容、恒显示。
        if (m_viewerTabs) {
            pruneDeadViewerTabs();
            ensureBrowserTab();
            if (imageTabCount() == 0) {
                if (!m_currentFile.isEmpty()) openViewerTab(m_currentFile);
            } else if (!m_viewerNoSync && !m_currentFile.isEmpty()) {
                syncViewerTab(m_currentFile);
            }
            m_viewerTabs->setVisible(true);
        }
    } else {
        m_preview->setMinimumWidth(200);
        if (m_viewerTabs) {
            // Interface/syncBrowser:关视图时把浏览器选中项同步到最后那个标签
            // (#105:当前落在「浏览器」标签上时没有可同步的路径,跳过)
            const int curTab = m_viewerTabs->currentIndex();
            if (AppSettings::instance().get("Interface/syncBrowser", false).toBool()
                && !isBrowserTab(curTab)) {
                const QString p = tabPath(curTab);
                if (!p.isEmpty()) m_fileGrid->selectByPath(p);
            }
            m_viewerTabs->hide();
            // 只藏不清表:退回浏览器再进来,那几张标签还在
        }
        // 恢复进入前的实际布局(硬编码重置会让用户拖好的分栏变掉)
        if (m_savedSplitter.size() == 3)
            m_splitter->setSizes(m_savedSplitter);
        else
            m_splitter->setSizes(mw_impl::defaultSplitterSizes());
        // 焦点必须显式还给网格:实测(cache/tmp/focus_probe.cpp)面板即使被 hide 过、
        // 即使策略降回 NoFocus，focusWidget 仍记在它身上 —— 不补这一句，
        // 退回浏览器后键还往面板送，方向键/空格看起来直接坏了(同 navigateTo 的纪律)
        if (!QApplication::activeModalWidget()) m_fileGrid->setFocus();
    }
    applyTitle();
    applyFullViewChrome();   // 模式切换不动窗口状态,changeEvent 不会来:这里主动算一遍
}

// ── #154:G = 全屏预览(2026-09-01 用户最终定义)──
// 与 F11 的"全屏"、查看器模式都不是一回事:G 直接把当前画面铺满整屏,
// 只留画面本身 —— 不进查看器、不碰标签页。再按 G 或 ESC **完全回到按 G
// 前的布局**(分栏/面板/菜单栏/标签条,全部走同一份进前存档)。
// 已处于 F11 全屏时按 G 也成立:showFullScreen 是 no-op,走下面的直接调用。
void MainWindow::toggleFullView() {
    if (m_fullView) { exitFullView(); return; }
    m_fullView = true;
    m_fullViewSplitter = m_splitter->sizes();
    showFullScreen();
    applyFullViewChrome();
    applyPaneVisibility();
    QList<int> sz { 0, 0, width() };
    m_splitter->setSizes(sz);
}

// 退出路径唯一:进前布局只存在 m_fullViewSplitter 一份。showNormal 会触发
// changeEvent,那里见到 m_fullView 已 false 只做 chrome 收放,不会抢在这里
// 前面把面板恢复掉 —— 面板恢复顺序:先按意图显隐,再还原分栏宽度。
void MainWindow::exitFullView() {
    if (!m_fullView) return;
    m_fullView = false;
    if (isFullScreen()) showNormal();
    applyPaneVisibility();
    if (m_fullViewSplitter.size() == 3)
        m_splitter->setSizes(m_fullViewSplitter);
    applyFullViewChrome();
}

// 全屏形态的 chrome 收放:G 全屏预览=菜单栏收掉;查看器+F11 全屏也只留画面
// (批 2 既有行为);F11 浏览器全屏保留菜单栏("把窗口铺满"不是"只看画面")。
void MainWindow::applyFullViewChrome() {
    const bool fs = isFullScreen();
    menuBar()->setVisible(!(fs && m_fullView) && !(fs && m_viewerMode));
    if (m_viewerTabs)
        m_viewerTabs->setVisible(m_viewerMode && !fs);
}

void MainWindow::changeEvent(QEvent* event) {
    if (event->type() == QEvent::WindowStateChange) {
        // 全屏被别的出口(F11/浮动工具条)关掉时,补完全屏预览的退出;
        // 否则只按当前形态收放 chrome
        if (m_fullView && !isFullScreen()) exitFullView();
        else applyFullViewChrome();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::viewerBack() {
    // ESC 行为按设置:查看器受 escCloseViewer 控,浏览器(含全屏)受 escCloseBrowser 控;
    // 两者都关时 ESC 什么都不做
    AppSettings& st = AppSettings::instance();
    // #154:全屏预览里 ESC = 退回进前布局(与再按 G 等效)
    if (m_fullView) { exitFullView(); return; }
    if (m_viewerMode) {
        // #108:全屏里 ESC 先退全屏,停在看图状态 —— 一步跳回浏览器会让人以为图丢了
        if (isFullScreen()) { showNormal(); return; }
        if (st.get("Keyboard/escCloseViewer", true).toBool()) toggleViewer();
        return;
    }
    if (isFullScreen() && st.get("Keyboard/escCloseBrowser", false).toBool())
        showNormal();
}
