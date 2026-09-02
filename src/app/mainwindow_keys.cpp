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
// 拖放(#81)
//   拖入文件 → 导航到所在目录并选中(同目录的多个一起选中)
//   拖入目录 → 直接进入
//   拖到文件夹上(网格卡片或树节点)→ 复制进去(与"拖出=复制"对称)
// ═══════════════════════════════════════════
void MainWindow::dragEnterEvent(QDragEnterEvent* e) {
    if (e->mimeData() && e->mimeData()->hasUrls()) e->acceptProposedAction();
}

// 可放置区域只有文件网格与文件夹树(含其子控件)。拖到别处一律 ignore ——
// Qt 在 dragMove 被 ignore 时自动换成"禁止通行"光标,松开也不会走到 dropEvent。
// 判定放这里而不是放 dragEnter:enter 必须 accept 一次,否则后续 move 收不到。
bool MainWindow::dropOnValidTarget(const QPoint& pos) const {
    QWidget* child = childAt(pos);
    if (!child) return false;
    for (QWidget* w : { static_cast<QWidget*>(m_fileGrid), static_cast<QWidget*>(m_folderTree) }) {
        if (w && (child == w || w->isAncestorOf(child))) return true;
    }
    return false;
}

void MainWindow::dragMoveEvent(QDragMoveEvent* e) {
    if (!(e->mimeData() && e->mimeData()->hasUrls())) return;
    if (dropOnValidTarget(e->position().toPoint())) e->acceptProposedAction();
    else e->ignore();
}

void MainWindow::dropEvent(QDropEvent* e) {
    const QMimeData* md = e->mimeData();
    if (!md || !md->hasUrls()) return;
    const QPoint gpos = e->position().toPoint();
    if (!dropOnValidTarget(gpos)) { e->ignore(); return; }

    QStringList paths;
    for (const QUrl& u : md->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = QDir::fromNativeSeparators(u.toLocalFile());
        if (QFileInfo::exists(p)) paths << p;
    }
    if (paths.isEmpty()) return;
    e->acceptProposedAction();

    // 落点是否压在某个文件夹上:先看网格,再看树
    QString dropIntoDir;
    if (QWidget* child = childAt(gpos)) {
        if (m_fileGrid && (child == m_fileGrid || m_fileGrid->isAncestorOf(child))) {
            const int idx = m_fileGrid->hitTest(m_fileGrid->mapFrom(this, gpos));
            const QString hit = m_fileGrid->pathAt(idx);
            if (!hit.isEmpty() && QFileInfo(hit).isDir()) dropIntoDir = hit;
        } else if (m_folderTree && (child == m_folderTree
                                    || m_folderTree->isAncestorOf(child))) {
            const QString hit = m_folderTree->pathAt(m_folderTree->mapFrom(this, gpos));
            if (!hit.isEmpty() && QFileInfo(hit).isDir()) dropIntoDir = hit;
        }
    }

    // 拖放语义(用户 2026-08-31 明令):拖放=移动,Ctrl+拖放=复制。
    // 落点压在文件夹上会改动文件;是否弹窗由 FileOps/dropConfirm 控制,
    // 弹窗文案按实际动作区分,并把"Ctrl+拖放=复制"写进提示里。
    // 拖到空白处只是导航,不改任何文件,不弹。
    // 网格与文件夹树两个落点都经这里,提示天然同时生效。
    if (!dropIntoDir.isEmpty()) {
        const bool copy = (QApplication::keyboardModifiers() & Qt::ControlModifier) != 0;
        const QString verb = copy ? QString::fromUtf8("复制") : QString::fromUtf8("移动");
        const QString what = paths.size() == 1
            ? QFileInfo(paths.first()).fileName()
            : QString::fromUtf8("%1 个项目").arg(paths.size());

        // 拖到自己所在目录没有意义,提前拦下(连弹窗都不出)
        QStringList actionable;
        for (const QString& p : paths) {
            if (QFileInfo(p).absolutePath()
                == QDir::fromNativeSeparators(dropIntoDir)) continue;
            actionable << p;
        }
        if (actionable.isEmpty()) return;

        if (AppSettings::instance().get("FileOps/dropConfirm", true).toBool()) {
            const QString tip = copy
                ? QString::fromUtf8("(松开 Ctrl 再拖即为移动)")
                : QString::fromUtf8("(按住 Ctrl 拖放即为复制)");
            if (QMessageBox::question(this, QString::fromUtf8("拖放%1").arg(verb),
                    QString::fromUtf8("将 %1 %2到\n%3 ?\n\n%4")
                        .arg(what, verb, dropIntoDir, tip),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                return;   // 用户取消:什么都不做
            }
        }

        QStringList errs;
        int done = 0;
        for (const QString& p : actionable) {
            const QFileInfo fi(p);
            const QString dst = dropIntoDir + "/" + fi.fileName();
            if (QFileInfo::exists(dst)) { errs << dst; continue; }
            // 移动 = rename(Windows MoveFileEx 跨盘也能走);复制 = 文件 copy、目录 rename
            const bool ok = copy ? (fi.isDir() ? QDir().rename(p, dst)
                                               : QFile::copy(p, dst))
                                 : (QDir().rename(p, dst) || QFile::rename(p, dst));
            if (!ok) { errs << dst; continue; }
            ++done;
        }
        if (done) {
            m_fileGrid->refreshCurrentDir();
            if (m_folderTree) m_folderTree->refreshCurrent();
            // 与删除提示同一套左下角 toast,反馈简短明确
            showDeleteToast(this, QString::fromUtf8("已%1 %2 项到目标文件夹").arg(verb).arg(done));
        }
        if (!errs.isEmpty())
            QMessageBox::warning(this,
                QString::fromUtf8("部分项目未能%1").arg(verb),
                errs.join(QLatin1Char('\n')));
        return;
    }

    // 拖到空白 = 导航到目标目录并选中拖进来的文件
    const QFileInfo first(paths.first());
    const QString dir = first.isDir() ? first.absoluteFilePath() : first.absolutePath();
    // 目录加载完成后按 m_preferPath 选中首项;同目录的其余项随后补选
    if (!first.isDir()) m_fileGrid->setPreferPath(first.absoluteFilePath());
    navigateTo(dir);
    if (!first.isDir()) {
        for (const QString& p : paths)
            if (QFileInfo(p).absolutePath() == dir) m_fileGrid->selectPathAdditive(p);
    }
}

// 用户自定义快捷键:ini "Shortcuts/<功能名>" 覆盖默认(设置→快捷键页编辑)
void MainWindow::applyShortcuts() {
    QSettings s = mw_impl::appSettings();
    QList<QAction*> acts;
    acts.append(menuBar()->actions());
    for (QAction* top : menuBar()->actions())
        if (QMenu* mm = top->menu())
            collectMenuActions(mm, acts);
    for (QAction* a : acts) {
        if (a->shortcut().isEmpty()) continue;
        QString key = QString("Shortcuts/") + a->text().remove('&');
        QString v = s.value(key).toString();
        if (!v.isEmpty())
            a->setShortcut(QKeySequence(v));
    }
}

void MainWindow::collectMenuActions(QMenu* menu, QList<QAction*>& out) {
    for (QAction* a : menu->actions()) {
        if (QMenu* sub = a->menu())
            collectMenuActions(sub, out);
        else if (!a->isSeparator() && a->text().isEmpty() == false)
            out.append(a);
    }
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
    // ── 2026-09-02 全屏胶片条:光标到顶显示/离开隐藏;条上点击/滚轮切文件 ──
    // MouseMove 落在全屏预览面板上时驱动显隐;label 的点击(释放)与滚轮单独收。
    if (m_filmStrip) {
        if (event->type() == QEvent::MouseMove && !m_filmStrip->underMouse()) {
            auto* me = static_cast<QMouseEvent*>(event);
            const QPoint mp = me->position().toPoint();
            updateFilmStrip(&mp);
        } else if (event->type() == QEvent::MouseButtonRelease
                   && m_filmStrip->isVisible()
                   && m_filmStrip->isAncestorOf(qobject_cast<QWidget*>(obj))) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() == Qt::LeftButton && obj != m_filmStrip) {
                QWidget* w = qobject_cast<QWidget*>(obj);
                if (w) {
                    const int idx = w->property("idx").toInt();
                    if (idx >= 0) { jumpToFilmItem(idx); return true; }
                }
            }
        } else if (event->type() == QEvent::Wheel
                   && m_filmStrip->isVisible()
                   && m_filmStrip->isAncestorOf(qobject_cast<QWidget*>(obj))) {
            auto* we = static_cast<QWheelEvent*>(event);
            const int delta = we->angleDelta().y();
            if (m_fileGrid) m_fileGrid->navigateSelection(delta > 0 ? -1 : 1);
            event->accept();
            return true;
        }
    }
    // 标签条中键 = 关掉那张(浏览器习惯;QTabBar 没有对应的信号)
    if (obj == m_viewerTabs && event->type() == QEvent::MouseButtonRelease) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::MiddleButton) {
            const int i = m_viewerTabs->tabAt(me->position().toPoint());
            if (i >= 0) { closeViewerTab(i); return true; }
        }
    }
    // 地址栏:第一次单击 = 全选整条路径(#109①);之后每次单击 = 把光标放到点上(#127)。
    // 一次"焦点期"内只自动全选一次 —— 旧写法每次都补全选,第三次点又变全选,
    // 用户就没法在路径中间改字了(用户报的原话:"第二次左键单击应该光标点进去")。
    // 写法顺序被实测钉住(E:/dev/gaze):QLineEdit 自己的
    // press 处理会当场把"整条选中"改成光标(sel=[0,20] → sel=0 caret=9),所以
    //   ① "按下前是否已整条选中"只能取在它之前;
    //   ② 全选要延后一拍做 —— release 里它还要把光标挪到点击处,当场 selectAll 会被抹掉;
    //   ③ 已全选时什么都不做:光标已由 QLineEdit 落到点击处,正是"看光标在哪"。
    if (m_addrBar && obj == m_addrBar) {
        if (event->type() == QEvent::FocusIn) {
            m_addrSelectedOnce = false;   // 新的一次编辑期:第一次点击才自动全选
        } else if (event->type() == QEvent::ShortcutOverride) {
            // #155:地址栏里任何形态的退格都是编辑键,不是「上级目录」。菜单
            // QAction(QKeySequence("Backspace")) 的匹配发生在按键送达控件之前,
            // 这里 accept 这一次 ShortcutOverride,快捷键系统才会收手
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Backspace) event->accept();
        } else if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            m_addrPressPt = me->position().toPoint();
            m_addrWasAllSelected = me->button() == Qt::LeftButton
                && m_addrBar->hasSelectedText()
                && m_addrBar->selectionStart() == 0
                && m_addrBar->selectionEnd() == m_addrBar->text().length();
        } else if (event->type() == QEvent::MouseButtonRelease) {
            auto* me = static_cast<QMouseEvent*>(event);
            // 按下和松开不在同一处 = 用户拖出了自己的选区,别用全选覆盖它
            const bool click = me->button() == Qt::LeftButton
                && (me->position().toPoint() - m_addrPressPt).manhattanLength() <= 4;
            if (click && !m_addrWasAllSelected && !m_addrSelectedOnce) {
                m_addrSelectedOnce = true;
                QLineEdit* bar = m_addrBar;
                QTimer::singleShot(0, bar, [bar]() { bar->selectAll(); });
            }
            m_addrWasAllSelected = false;
        }
    }
    if (event->type() == QEvent::KeyPress) {
        auto *ke = static_cast<QKeyEvent*>(event);
        auto *tgt = qobject_cast<QWidget*>(obj);
        // #155:地址栏里 Backspace = 删字(外部改键工具送的是 Alt+退格)。实测
        // (cache/tmp/altbs_probe3.cpp):QLineEdit 收到 Alt+退格既不删字也不接受,
        // 事件冒泡上去,在 Gaze 里就走出"上级目录"的假动作 —— 这里替它删并消费,
        // 放在 bypass 之前:编辑手段优先于一切(#61 原则)。backspace() 有选中先
        // 删选中,与原生裸退格行为一致;Ctrl+退格(删词)不拦,仍归 QLineEdit。
        if (obj == m_addrBar
            && ke->key() == Qt::Key_Backspace
            && (ke->modifiers() == Qt::NoModifier
                || ke->modifiers() == Qt::AltModifier)) {
            m_addrBar->backspace();
            return true;
        }
        // 路由只看"这个键送给了谁",不看 QApplication::focusWidget():焦点在别处、
        // 键却发给弹窗的情况(QMenu/下拉列表都不是 QDialog)旧写法会整段吞掉 Enter/Esc。
        // main.cpp 的对话框过滤器装得更早、先触发,这里是第二道闸。
        const bool bypass = !tgt
            || ke->isAutoRepeat()                    // 按住不放:不该反复刷标记、反复起幻灯片
            || insideDialog(tgt)
            || QApplication::activePopupWidget()     // 有弹窗开着,键盘归弹窗
            // 查看器的键归查看器表:默认表里"适应窗口"就是 F，而浏览器 F=红标，
            // 不让路的话设置页那张表配了什么都会被浏览器键位吞掉
            || (m_viewerMode && m_preview->claimsHotkey(ke));
        if (!bypass) {
            // 键盘就是这些控件的输入手段,字母/Space/Enter/Esc 一律不抢
            const bool forText = textInputWidget(tgt);
            // Space/Enter 在按钮/列表/滑块里有本职动作(激活、选中、就地编辑)
            const bool forActivation = forText || activationKeyWidget(tgt);
            // Keyboard/space:0 播放/暂停(默认) 1 什么都不做 2 下一个文件 3 快速幻灯片
            // (选项表与设置→快捷键→空格一致)
            // 只拦不带 Ctrl/Alt/Meta 的空格:Alt+Space 是系统窗口菜单
            if (!forActivation
                && ke->key() == Qt::Key_Space
                && (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier
                                       | Qt::MetaModifier)) == 0) {
                switch (AppSettings::instance().get("Keyboard/space", 0).toInt()) {
                // "什么都不做"必须真的不做:旧写法 return true 写在 switch 外,
                // 选了这一项空格照样被吞,控件自己的滚动/选择也收不到
                case 1: break;
                case 2: m_fileGrid->navigateSelection(1); return true;
                case 3: toggleSlideshow(); return true;
                default: m_preview->togglePlayPause(); return true;
                }
            }
            // Ctrl+F = 内联搜索条(#107):焦点在地址栏/树/网格上都发起;文本框里
            // 也不例外(这是命令不是打字)。查看器形态下文件列表是藏着的,不接。
            if (!m_viewerMode && (ke->modifiers() & Qt::ControlModifier)
                && ke->key() == Qt::Key_F) {
                m_fileGrid->startFind();
                return true;
            }
            if (!forText) {
                // 颜色标记快捷键:Ctrl+0~5(0=取消)/ F=红 / D=取消;Ctrl+PgUp/PgDn 快退快进
                if (ke->modifiers() & Qt::ControlModifier) {
                    if (ke->key() >= Qt::Key_0 && ke->key() <= Qt::Key_5) {
                        applyColorLabel(ke->key() - Qt::Key_0);
                        return true;
                    }
                    // Viewer/seekSeconds:一次跳多少秒(设置→键盘;默认 3)
                    if (ke->key() == Qt::Key_PageUp) {
                        m_preview->seekDelta(-seekSeconds()); return true;
                    }
                    if (ke->key() == Qt::Key_PageDown) {
                        m_preview->seekDelta(seekSeconds()); return true;
                    }
                    // Ctrl+W = 关闭当前标签卡(2026-09-01 用户令)。落在「浏览器」
                    // 标签或浏览器模式时没有可关的内容标签,按键落空 —— 浏览器
                    // 标签是回标准模式的出口,不是内容;关到最后一张图片标签时
                    // closeViewerTab 自己会退回浏览器
                    if (ke->key() == Qt::Key_W && m_viewerMode && m_viewerTabs
                        && !isBrowserTab(m_viewerTabs->currentIndex())) {
                        closeViewerTab(m_viewerTabs->currentIndex());
                        return true;
                    }
                } else if (ke->modifiers() == Qt::NoModifier) {
                    if (ke->key() == Qt::Key_F) { applyColorLabel(1); return true; }
                    if (ke->key() == Qt::Key_D) { applyColorLabel(0); return true; }
                    // G=全屏预览(#154):直接铺满只留画面,不进查看器不碰标签;
                    // 再按 G/ESC 完全回到按 G 前的布局。走这道过滤器而不是菜单
                    // QAction 的 shortcut:上面那几层 forText/弹窗判断才是"裸键
                    // 不该抢文本框"的防线(#61)
                    if (ke->key() == Qt::Key_G) { toggleFullView(); return true; }
                    // 回车:按 SwitchMode/enterKey 切换模式
                    if ((ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
                        && !forActivation) {
                        // #154:全屏预览里切模式会改掉"退出还原的布局",禁用
                        if (m_fullView) return true;
                        // #128②:刚用地址栏跳过路径,这一下就不再切模式 ——
                        // 跳转会把焦点交给网格并自动选中第一项(#35),同一个物理
                        // 回车接着落到网格上会被再消费一次,用户看到的就是
                        // "跳转的同时查看器又打开了那个视频"。
                        if (QDateTime::currentMSecsSinceEpoch() - m_lastAddrJumpMs < 500)
                            return true;
                        requestSwitchMode("SwitchMode/enterKey");
                        return true;
                    }
                    // Esc=退出查看器:只让文本类控件(弹窗/对话框上面已经整体放行)
                    if (ke->key() == Qt::Key_Escape) { viewerBack(); return true; }
                    // #136:重命名。F2=重命名(与资源管理器惯例一致;2026-09-02 用户定版)。
                    // 可配主键在编辑菜单「重命名」QAction(见 mainwindow_menus.cpp,Shortcuts/重命名)。
                    // 这里保留带守卫的硬编码,好让 F2 在文本框/弹窗里不抢键 —— Qt 的
                    // QAction shortcut 不经过 forText 三道闸,菜单裸键会吞文本框的照删键。
                    if (ke->key() == Qt::Key_F2) { renameFocused(); return true; }
                    // F3 = 预览面板开关(2026-09-02 用户定版):浏览器形态下切"预览"面板显隐;
                    // 查看器/全屏形态预览面板本就隐藏,不响应。
                    if (ke->key() == Qt::Key_F3 && !m_viewerMode && !m_fullView) {
                        setPaneVisible("preview", !paneVisible("preview"));
                        return true;
                    }
                }
            }
        }
    }
    return QMainWindow::eventFilter(obj, event);
}
