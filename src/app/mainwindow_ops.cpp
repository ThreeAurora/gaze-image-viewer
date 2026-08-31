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

// ── 切换模式:设置→交互→切换模式(SwitchMode/doubleClick|middleClick|enterKey) ──
// 规格 0 浏览器↔全屏|查看器↔全屏  1 浏览器↔查看器  2 浏览器→全屏→查看器
//      3 浏览器→查看器→全屏      4 什么都不做      5 用系统程序打开
void MainWindow::requestSwitchMode(const QString& triggerKey) {
    // 未设置时的兜底:双击=进查看器(#102,图像/视频才会走到这儿);
    // 中键是"什么都不做";回车=浏览器↔查看器。
    // 5 仍然可选:"用系统程序打开"在设置页里照样配(把图像丢给外部编辑器是有人要的)。
    const QVariant defSpec = triggerKey == "SwitchMode/doubleClick" ? 1
                           : triggerKey == "SwitchMode/middleClick" ? 4 : 1;
    const int spec = AppSettings::instance().get(triggerKey, defSpec).toInt();
    Logger::event(QStringLiteral("switchMode trigger=%1 spec=%2 viewerNow=%3")
                      .arg(triggerKey).arg(spec).arg(m_viewerMode));   // #128② 取证
    if (spec == 4) return;
    if (spec == 5) {   // 仅"用系统程序打开"针对文件;目录仍然进入
        const auto paths = m_fileGrid->selectedPaths();
        if (!paths.isEmpty()) openWithSystem(paths.first());
        return;
    }
    cycleMode(spec);
}

void MainWindow::cycleMode(int spec) {
    switch (spec) {
    case 0:   // 浏览器↔全屏 | 查看器↔全屏:当前模式内切换全屏
        if (isFullScreen()) showNormal(); else enterFullscreen();
        return;
    case 1:   // 浏览器↔查看器
        toggleViewer();
        return;
    case 2: { // 浏览器→全屏→查看器→浏览器
        if (!isFullScreen() && !m_viewerMode) { toggleViewer(); toggleViewer(); enterFullscreen(); }
        else if (isFullScreen() && !m_viewerMode) { toggleViewer(); }
        else if (m_viewerMode && isFullScreen()) { showNormal(); toggleViewer(); }
        else { enterFullscreen(); }
        return;
    }
    case 3: { // 浏览器→查看器→全屏→浏览器
        if (!m_viewerMode && !isFullScreen()) toggleViewer();
        else if (m_viewerMode && !isFullScreen()) enterFullscreen();
        else { if (isFullScreen()) showNormal(); if (m_viewerMode) toggleViewer(); }
        return;
    }
    default:
        toggleViewer();
    }
}

// ── 快速幻灯片(Keyboard/space=快速幻灯片) ──
void MainWindow::toggleSlideshow() {
    m_slideshow = !m_slideshow;
    if (m_slideshow) {
        m_slideTimer.setInterval(mw_impl::slideIntervalMs());
        m_slideTimer.start();
    } else {
        m_slideTimer.stop();
    }
}

void MainWindow::reloadAfterDelete(const QString& deletedPath) {
    if (m_fileGrid) m_fileGrid->reloadAfterDelete({ deletedPath });
}

// Viewer/seekSeconds(设置→键盘):快进/快退一次跳多少秒,默认 3
int MainWindow::seekSeconds() const {
    return qBound(1, AppSettings::instance().get("Viewer/seekSeconds", 3).toInt(), 3600);
}

// #136:F2/F3 的统一出口。旧写法只有 renameCurrent() 且**只读网格选区** —— 焦点在
// 文件树上时按重命名，改的其实是网格里残留的那一项（树和网格是两套选中状态）。
// 所以这里按"焦点落在谁家里"路由。
// F3 走的是 QAction 快捷键(为了能进设置→快捷键配置页和 Shortcuts/ ini 覆盖)，
// 那条路**不经过** eventFilter 的 forText/弹窗三道闸，故在此补一道同样口径的守卫：
// 地址栏/内联搜索条/任何弹窗里按 F3 不该改名。
void MainWindow::renameFocused() {
    QWidget* f = QApplication::focusWidget();
    // 只挡"键盘是输入手段"的那类控件与弹窗。故意**不**用 activationKeyWidget：
    // 它含 QAbstractItemView，而文件树/文件网格正是 item view —— 用它等于两处都改不了名。
    // 与 keytarget.h 的既有口径一致："F/D/F2 这类键对 item view 仍全局生效"。
    if (f && (textInputWidget(f) || insideDialog(f))) return;
    if (f && m_folderTree && m_folderTree->isAncestorOf(f)) {
        m_folderTree->renameSelected();
        return;
    }
    renameCurrent();
}

// ── 重命名当前选中项:FileOps/renameDialog 决定弹对话框还是卡片上就地改
void MainWindow::renameCurrent() {
    const auto paths = m_fileGrid->selectedPaths();
    if (paths.isEmpty()) return;
    if (!AppSettings::instance().get("FileOps/renameDialog", true).toBool()) {
        m_fileGrid->beginInlineRename();
        return;
    }
    QFileInfo fi(paths.first());
    bool ok = false;
    const QString name = QInputDialog::getText(this, QString::fromUtf8("重命名"),
        QString::fromUtf8("新名称:"), QLineEdit::Normal, fi.fileName(), &ok).trimmed();
    if (!ok || name.isEmpty() || name == fi.fileName()) return;
    // 校验必须先于拼路径:"a/b" 会让下面的 rename 把文件搬到别处,界面上毫无动静
    if (const QString why = invalidNameReason(name); !why.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"), why);
        return;
    }
    const QString np = QDir(fi.absolutePath()).filePath(name);
    if (QFileInfo::exists(np)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
                             QString::fromUtf8("目标名已存在:\n") + np);
        return;
    }
    if (!QFile::rename(paths.first(), np)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名失败"), np);
        return;
    }
    m_fileGrid->setPreferPath(np);
    m_fileGrid->refreshCurrentDir();
}

// ═══════════════════════════════════════════
// 颜色标记 / 最近文件 / 系统打开 / 红标三态
// ═══════════════════════════════════════════
void MainWindow::applyColorLabel(int color) {
    m_fileGrid->applyColorLabelToSelection(color);
    updateStatus();
}

void MainWindow::ensureRecentLoaded() {
    if (m_recentLoaded) return;
    QSettings s = mw_impl::appSettings();
    m_recentList = s.value("recent/files").toStringList();
    // 上限也在这一刻读一次:菜单可能整场会话都没打开过,不能等它来告知上限
    m_recentMax = qMax(0, s.value("Interface/maxRecent", 20).toInt());
    m_recentLoaded = true;
}

// 上限以设置页为准:改小之后菜单一打开就跟上,截断结果走同一个防抖落盘。
// (此前是"菜单直接改 ini",而内存副本没变,下一次 flush 又把长列表写回去)
void MainWindow::trimRecentList() {
    ensureRecentLoaded();
    m_recentMax = qMax(0, AppSettings::instance()
                            .get("Interface/maxRecent", 20).toInt());
    if (m_recentList.size() > m_recentMax) {
        m_recentList = m_recentList.mid(0, m_recentMax);
        m_recentFlushTimer.start();
    }
}

void MainWindow::addRecentFile(const QString& path) {
    if (path.isEmpty()) return;
    QFileInfo fi(path);
    if (fi.isDir()) return;

    // 性能:只在内存列表上操作(微秒级),写盘由定时器合批;
    // 上限数量懒加载时读一次 ini(此前每次选中变化都新开 QSettings 读盘,
    // 违反项目"逐条目路径禁磁盘 IO"铁律——perf.log 926ms layoutCards 同源教训)
    ensureRecentLoaded();
    m_recentList.removeAll(path);
    m_recentList.prepend(path);
    while (m_recentList.size() > m_recentMax) m_recentList.removeLast();
    m_recentFlushTimer.start();   // 单发防抖:停止切换 500ms 后统一落盘
}

void MainWindow::flushRecentFiles() {
    if (!m_recentLoaded) return;   // 本会话没动过最近文件
    m_recentFlushTimer.stop();
    // 落盘前按当前上限再校一次:设置页改小上限后不必等菜单打开才生效
    m_recentMax = qMax(0, AppSettings::instance()
                            .get("Interface/maxRecent", 20).toInt());
    while (m_recentList.size() > m_recentMax) m_recentList.removeLast();
    QSettings s = mw_impl::appSettings();
    s.setValue("recent/files", m_recentList);
}

void MainWindow::rebuildRecentMenu(QMenu* menu) {
    trimRecentList();     // 上限以设置页为准;截断走同一个防抖落盘
    menu->clear();
    if (m_recentList.isEmpty()) {
        menu->addAction(QString::fromUtf8("(空)"))->setEnabled(false);
        return;
    }
    for (const auto& p : m_recentList) {
        menu->addAction(p, this, [this, p]() {
            QFileInfo fi(p);
            if (!fi.exists()) {
                QMessageBox::information(this, QString::fromUtf8("最近的文件"),
                    QString::fromUtf8("文件不存在:\n") + p);
                return;
            }
            navigateTo(fi.absolutePath());
            m_fileGrid->selectByPath(p);
        });
    }
}

void MainWindow::openWithSystem(const QString& path) {
    if (!path.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

void MainWindow::cycleRedFilter() {
    // 三态循环:显示全部 → 仅红色标记 → 仅未红标 → 显示全部
    m_redFilterMode = (m_redFilterMode + 1) % 3;
    switch (m_redFilterMode) {
    case 0: m_fileGrid->setFilterMode(FILTER_ALL); break;
    case 1: m_fileGrid->setFilterMode(FILTER_RED); break;
    case 2: m_fileGrid->setFilterMode(FILTER_UNRED); break;
    }
}
