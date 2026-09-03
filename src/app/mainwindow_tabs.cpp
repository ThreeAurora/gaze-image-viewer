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
#include "i18n.h"

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
// 查看器标签卡(Interface/multiViewerTabs / oneViewerTab / syncBrowser)
//   两个开关各管一件事(与 XnView 同义,别把它们当互为反面):
//     multiViewerTabs 关(默认) = 一个文件只占一个标签:查看器内导航到它,
//                                切到已有那个标签,而不是再开一个同文件的标签
//                       开     = 允许同一文件占多个标签
//     oneViewerTab    开       = 一批文件只装一个标签,后开的就地顶掉当前标签
//   查看器内的导航(方向键/列表选中)永远不追加标签:表非空时只把当前那张就地
//   改成目标(syncViewerTab)。想让表变长,只有"在新标签卡中打开"(浏览器右键 /
//   预览区右键)与"从浏览器再进查看器看另一张"(2026-09-03 用户令:已开的标签
//   必须留着,改为激活或追加)这两个入口 —— 翻 500 张图不该留下 500 张标签。
//   标签表跨"退回浏览器"保留,且退回浏览器后标签栏**继续显示**(updateTabBarVis):
//   用户点「浏览器」标签只是回到标准模式,他的文件标签必须一直看得见、点得着。
//   Interface/maxViewerTabs(2~99,默认99;旧ini的0=不限按99算)只约束追加新标签时。
//   路径存在 tabData 里(不是并行数组),所以 removeTab/拖拽重排不需要任何索引修正。
//   #105:索引 0 常驻「浏览器」标签(tabData=哨兵),用户明令"点它回标准模式"。
//   它无 × 按钮、中键/右键关闭跳过、拖拽后归位;凡按 tabData 判"是不是真文件"的
//   地方都必须过 isBrowserTab,否则哨兵会被当死文件摘掉/被 syncViewerTab 覆写。
// ═══════════════════════════════════════════

bool MainWindow::isBrowserTab(int index) const {
    return m_viewerTabs && index >= 0 && index < m_viewerTabs->count()
        && m_viewerTabs->tabData(index).toString() == mw_impl::kBrowserTabData;
}

// 标签栏显隐总闸(2026-09-03 用户令):查看器模式恒显示;浏览器模式只要还有
// 图片标签就显示 —— 点「浏览器」标签回浏览器后,文件标签必须看得见、点得着,
// 一张不剩才收。全屏(含 G 全屏预览)沿用旧规矩:整个收掉,只留画面。
void MainWindow::updateTabBarVis() {
    if (!m_viewerTabs) return;
    m_viewerTabs->setVisible(!isFullScreen()
                             && (m_viewerMode || imageTabCount() > 0));
}

int MainWindow::firstImageTab() const {
    if (!m_viewerTabs) return -1;
    for (int i = 0; i < m_viewerTabs->count(); ++i)
        if (!isBrowserTab(i)) return i;
    return -1;
}

int MainWindow::imageTabCount() const {
    if (!m_viewerTabs) return 0;
    int n = 0;
    for (int i = 0; i < m_viewerTabs->count(); ++i)
        if (!isBrowserTab(i)) ++n;
    return n;
}

void MainWindow::ensureBrowserTab() {
    if (!m_viewerTabs) return;
    if (isBrowserTab(0)) return;
    const int b = indexOfTabPath(mw_impl::kBrowserTabData);
    if (b >= 0) {   // 在但不在 0(拖拽钩子漏网的残留):归位
        m_viewerTabs->blockSignals(true);
        m_viewerTabs->moveTab(b, 0);
        m_viewerTabs->blockSignals(false);
        return;
    }
    // 插入会让 current 平移,QTabBar 空表插入还会发 currentChanged ——
    // 全挡住:当前落位由调用方(openViewerTab 的 setCurrentIndex)显式做
    m_viewerTabs->blockSignals(true);
    const int i = m_viewerTabs->insertTab(0, gazeTr("浏览器"));
    m_viewerTabs->setTabData(i, mw_impl::kBrowserTabData);
    m_viewerTabs->setTabToolTip(i, gazeTr("返回浏览器(标准模式)"));
    m_viewerTabs->blockSignals(false);
}

// #105:标签名左侧的小缩略图。enqueue 走 Thumbnailer 全管线(网格刚展示过的
// 文件几乎必命中缓存,worker 线程立刻回图);图标由 ctor 里的 thumbnailReady
// 连接按路径回填。视频标签给视频帧图(VIDEO_EXTS 按扩展名判)。
void MainWindow::requestTabThumb(const QString& path) {
    QString su = QFileInfo(path).suffix().toLower();
    if (!su.isEmpty()) su.prepend(QLatin1Char('.'));
    Thumbnailer::instance().enqueue(path, 32, VIDEO_EXTS.count(su) > 0);
}

QString MainWindow::tabPath(int index) const {
    if (!m_viewerTabs || index < 0 || index >= m_viewerTabs->count()) return QString();
    return m_viewerTabs->tabData(index).toString();
}

int MainWindow::indexOfTabPath(const QString& path) const {
    if (!m_viewerTabs) return -1;
    for (int i = 0; i < m_viewerTabs->count(); ++i)
        if (m_viewerTabs->tabData(i).toString() == path) return i;
    return -1;
}

void MainWindow::installTabCloseButton(int index) {
    // 不用 setTabsClosable:系统提供的 × 图标准在深色标签上几乎看不见,
    // 与面板标题条的关闭按钮同款自绘"×",配色走主题常量
    auto* x = new QToolButton;
    x->setText(gazeTr("×"));
    x->setAutoRaise(true);
    x->setFocusPolicy(Qt::NoFocus);
    x->setCursor(Qt::ArrowCursor);
    x->setStyleSheet(QString(
        "QToolButton{border:none;background:transparent;color:%1;font-size:14px;padding:0 2px;}"
        "QToolButton:hover{color:%2;background:%3;border-radius:3px;}")
        .arg(C_TEXT_HIDDEN, C_TEXT, C_CARD_HOVER));
    connect(x, &QToolButton::clicked, this, [this, x]() {
        // 按按钮指针回查索引:点击前可能已有标签被关掉或被拖动重排
        for (int k = 0; k < m_viewerTabs->count(); ++k)
            if (m_viewerTabs->tabButton(k, QTabBar::RightSide) == x) {
                closeViewerTab(k);
                return;
            }
    });
    m_viewerTabs->setTabButton(index, QTabBar::RightSide, x);
}

int MainWindow::addViewerTab(const QString& path) {
    const int i = m_viewerTabs->addTab(QFileInfo(path).fileName());
    m_viewerTabs->setTabData(i, path);
    m_viewerTabs->setTabToolTip(i, path);
    m_viewerTabs->setTabIcon(i, QIcon());   // 就地复用索引时旧缩略图不能残留
    requestTabThumb(path);                  // #105:标签名左侧小缩略图(异步回填)
    installTabCloseButton(i);
    return i;
}

void MainWindow::setViewerTabPath(int index, const QString& path) {
    if (!m_viewerTabs || index < 0 || index >= m_viewerTabs->count()) return;
    m_viewerTabs->setTabData(index, path);
    m_viewerTabs->setTabText(index, QFileInfo(path).fileName());
    m_viewerTabs->setTabToolTip(index, path);
    m_viewerTabs->setTabIcon(index, QIcon());
    requestTabThumb(path);                  // #105:文件换了,缩略图跟着换
}

void MainWindow::closeViewerTab(int index) {
    if (!m_viewerTabs || index < 0 || index >= m_viewerTabs->count()) return;
    if (isBrowserTab(index)) return;        // #105:浏览器标签不可关(它就是出口)
    m_viewerTabs->removeTab(index);   // 索引修正和 tab 上的 × 按钮都由 QTabBar 自己收尾
    // #105:count>0 不再等价"还有图可看"——常驻标签兜着底;关到最后一张图片
    // 标签才算关完 = 退回浏览器(摘除引发 currentChanged→浏览器标签→toggleViewer
    // 的路径已经先退过了,这里只兜 current 不在那条路径上的情形)
    if (imageTabCount() > 0) return;
    if (m_viewerMode) toggleViewer();
    else updateTabBarVis();   // 浏览器态关光图签:标签栏按总闸收起
}

void MainWindow::pruneDeadViewerTabs() {
    if (!m_viewerTabs) return;
    // 标签表跨"退回浏览器"保留,期间文件可能在别处被删/移走。
    // 只在进查看器时逐个 stat(表长受 maxViewerTabs 约束,不在导航热路径上)
    // 摘除期间挡掉 currentChanged:调用方(toggleViewer 进查看器)马上就要自己
    // 定当前标签,这里每摘一张就顺手解码另一张并挪浏览器选中项是帮倒忙
    m_viewerTabs->blockSignals(true);
    for (int i = m_viewerTabs->count() - 1; i >= 0; --i) {
        if (isBrowserTab(i)) continue;      // #105:常驻标签不参与失效摘除
        if (!QFileInfo::exists(m_viewerTabs->tabData(i).toString()))
            m_viewerTabs->removeTab(i);
    }
    m_viewerTabs->blockSignals(false);
    // 摘完可能一张不剩(文件在别处被删/移走)。查看器模式下留一条空标签栏
    // 就是用户明确不要的那种"空白栏",这里跟着实际张数收口
    updateTabBarVis();
}

void MainWindow::syncViewerTab(const QString& path) {
    if (!m_viewerTabs || path.isEmpty()) return;
    const bool multiTab = AppSettings::instance()
                            .get("Interface/multiViewerTabs", false).toBool();
    int cur = m_viewerTabs->currentIndex();
    if (cur >= 0 && isBrowserTab(cur))
        cur = firstImageTab();   // #105:浏览器标签不是可就地覆写的同步目标
    if (cur < 0 || cur >= m_viewerTabs->count()) {   // 还没有图片标签:按开关开一个
        openViewerTab(path);
        return;
    }
    if (tabPath(cur) == path) return;
    // 导航不产生新标签。但 multiViewerTabs=关 时"一个文件只占一个标签"在导航里
    // 同样成立:把当前标签就地改成别处已开着的文件,等于凭空多出一个同文件标签。
    // oneViewerTab 不在这读:该模式下标签表本来就只有一项,就地替换即是它的语义。
    const int dup = multiTab ? -1 : indexOfTabPath(path);
    if (dup >= 0) {
        // 上面 onSelectionChanged 已 loadFile(path) 并同步 m_currentFile/标题,
        // 这里只把标签条的当前项挪过去。不挡信号会让 currentChanged 再解一遍同一张图。
        m_viewerTabs->blockSignals(true);
        m_viewerTabs->setCurrentIndex(dup);
        m_viewerTabs->blockSignals(false);
        return;
    }
    setViewerTabPath(cur, path);
}

void MainWindow::openViewerTab(const QString& path) {
    if (!m_viewerTabs || path.isEmpty()) return;
    // 从浏览器调用(右键"在新标签卡中打开"):先入查看器,否则标签条是隐藏的。
    if (!m_viewerMode) {
        // 先让"当前文件"就是目标,toggleViewer 才会拿它建首个标签(与"带文件启动"同句式)。
        // m_viewerNoSync:表非空时 toggleViewer 会就地改当前标签 —— 那是"进查看器看这张"
        // 的语义,会把本次要"另开一张"的意图吃掉。这里关掉它,标签由下面追加。
        if (m_currentFile != path) m_fileGrid->selectByPath(path);
        m_viewerNoSync = true;
        toggleViewer();
        m_viewerNoSync = false;
        if (!m_viewerMode) return;   // 没能进入查看器就别改标签表
    }
    AppSettings& st = AppSettings::instance();
    ensureBrowserTab();   // #105:任何建签路径都先保底索引 0 的「浏览器」标签
    const bool oneTab   = st.get("Interface/oneViewerTab", false).toBool();
    const bool multiTab = st.get("Interface/multiViewerTabs", false).toBool();
    if (oneTab && imageTabCount() > 0) {   // 只留一个标签:后开的顶掉当前的
        int idx = m_viewerTabs->currentIndex();
        if (idx < 0 || isBrowserTab(idx)) idx = firstImageTab();
        setViewerTabPath(idx, path);
        m_viewerTabs->setCurrentIndex(idx);   // current 被浏览器标签占着时拉回图签
        return;
    }
    // multiViewerTabs=关:该文件已有标签就激活它,不再开第二个
    const int dup = multiTab ? -1 : indexOfTabPath(path);
    if (dup >= 0) { m_viewerTabs->setCurrentIndex(dup); return; }
    // Interface/maxViewerTabs(2~99,2026-09-04 用户令)。超出时丢最老的一张
    // (不当场丢新开的,用户点的是"打开这个文件",结果必须看得见它)。
    // 旧 ini 里存的 0=不限按 99 算:99 张实际用不到头,语义等价
    int cap = st.get("Interface/maxViewerTabs", 99).toInt();
    if (cap < 2 || cap > 99) cap = 99;
    m_viewerTabs->blockSignals(true);   // 摘旧标签会挪 currentIndex,同一张图没必要再解一遍
    while (cap > 0 && imageTabCount() >= cap) {
        int victim = firstImageTab();   // #105:只摘图片标签,浏览器标签和正看着的这张除外
        if (victim == m_viewerTabs->currentIndex())
            for (int k = victim + 1; k < m_viewerTabs->count(); ++k)
                if (!isBrowserTab(k)) { victim = k; break; }
        m_viewerTabs->removeTab(victim);
    }
    m_viewerTabs->blockSignals(false);
    m_viewerTabs->setCurrentIndex(addViewerTab(path));
}

// ── 2026-09-02 用户令:双击预览区 / Ctrl+双击 ──
// 双击 = 进查看器并选中新标签(openViewerTab 的"从浏览器进入"段正好做这个)。
// Ctrl+双击 = 只把标签记下,人不进查看器:2026-09-03 起浏览器态只要还有图片
// 标签,标签栏就会当场浮现(updateTabBarVis),但模式与焦点都留在浏览器,
// 滚轮/方向键不受影响。
Q_INVOKABLE void MainWindow::openTabForeground() {
    if (m_currentFile.isEmpty()) return;
    openViewerTab(m_currentFile);
}

Q_INVOKABLE void MainWindow::openTabBackground() {
    if (m_currentFile.isEmpty() || !m_viewerTabs) return;
    ensureBrowserTab();
    AppSettings& st = AppSettings::instance();
    const bool oneTab   = st.get("Interface/oneViewerTab", false).toBool();
    const bool multiTab = st.get("Interface/multiViewerTabs", false).toBool();
    int cap = st.get("Interface/maxViewerTabs", 99).toInt();
    if (cap < 2 || cap > 99) cap = 99;   // 与 openViewerTab 同一钳制(旧 0=不限 → 99)
    if (oneTab) { openViewerTab(m_currentFile); return; }   // 单签模式没有"后台"可言
    const int dup = multiTab ? -1 : indexOfTabPath(m_currentFile);
    if (dup >= 0) return;                                    // 已有该文件标签:不重复开
    m_viewerTabs->blockSignals(true);
    while (cap > 0 && imageTabCount() >= cap) {
        int victim = firstImageTab();
        if (victim == m_viewerTabs->currentIndex()) {
            int k;
            for (k = victim + 1; k < m_viewerTabs->count(); ++k)
                if (!isBrowserTab(k)) { victim = k; break; }
            if (k >= m_viewerTabs->count()) victim = -1;
        }
        if (victim < 0) break;
        m_viewerTabs->removeTab(victim);
    }
    m_viewerTabs->blockSignals(false);
    addViewerTab(m_currentFile);   // 追加,但不 setCurrentIndex —— 焦点留在浏览器
    updateTabBarVis();             // 浏览器态开的第一张图签:标签栏当场浮现
}
