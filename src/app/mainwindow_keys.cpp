#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "views/filmstrip.h"   // #203:eventFilter 里胶片条显隐驱动要用它的接口
#include "previewpanel.h"
#include "imgsearchdialog.h"
#include "printdialog.h"
#include "infopanel.h"
#include "shelldelete.h"   // showDeleteToast:拖放复制成功的左下角提示
#include "filelockrelease.h"   // #214:拖放移动前放掉预览握着的句柄
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

// 落点是否"有意义"(2026-09-03):落点必须是"有地方可挪、且挪得有意义"才放行。
//   · 文件页空白 → 内部起拖(源=网格):目标=当前目录本身,自己移自己 → 禁止;
//                  外部拖入(资源管理器):保持"空白=导航过去"的原语义 → 放行
//   · 文件页非文件夹卡片 → 没地方可挪 → 禁止(两种来源一律禁止)
//   · 树/网格里"被拖文件所在目录"节点 → 自己移/复制到自己 → 禁止(**含外部拖入**)
//   · 被拖目录本身(self-drop:把 A 夹拖到树里的 A 夹上)→ 禁止
bool MainWindow::dropTargetMeaningful(const QPoint& pos, const QDropEvent* e) const {
    const bool internal = (e->source() == m_fileGrid);
    const QMimeData* md = e->mimeData();
    if (!md || !md->hasUrls()) return false;

    // 落点必须命中一个文件夹(网格卡片或树节点),否则没地方可挪
    QString dropInto;
    QWidget* child = childAt(pos);
    if (child && m_fileGrid && (child == m_fileGrid || m_fileGrid->isAncestorOf(child))) {
        const int idx = m_fileGrid->hitTest(m_fileGrid->mapFrom(this, pos));
        const QString hit = m_fileGrid->pathAt(idx);
        if (!hit.isEmpty() && QFileInfo(hit).isDir()) dropInto = hit;
    } else if (child && m_folderTree && (child == m_folderTree || m_folderTree->isAncestorOf(child))) {
        const QString hit = m_folderTree->pathAt(m_folderTree->mapFrom(this, pos));
        if (!hit.isEmpty() && QFileInfo(hit).isDir()) dropInto = hit;
    }
    // 语义(注释即是意图,2026-09-03 夜修复:此前写成 return internal 恰好相反,
    // 导致内部起拖落在文件页空白被放行(不显示禁止光标),外部拖入空白反被禁止):
    //   内部起拖(源=网格)→ 空白=目标即当前目录,自己移自己 → 禁止
    //   外部拖入     → 空白=导航过去(原语义) → 放行
    if (dropInto.isEmpty()) return !internal;

    // 任一被拖文件就躺在目标文件夹里(自己移到自己),或目标就是被拖目录本身
    // (把 A 夹挪进 A 夹),无论内外部拖入一律禁止
    const QString target = QDir::fromNativeSeparators(dropInto);
    for (const QUrl& u : md->urls()) {
        if (!u.isLocalFile()) continue;
        const QString p = QDir::fromNativeSeparators(u.toLocalFile());
        if (p.compare(target, Qt::CaseInsensitive) == 0) return false;   // 夹拖到自己身上
        if (QFileInfo(p).absolutePath().compare(target, Qt::CaseInsensitive) == 0)
            return false;                                                 // 就在目标里
    }
    return true;
}

void MainWindow::dragMoveEvent(QDragMoveEvent* e) {
    if (!(e->mimeData() && e->mimeData()->hasUrls())) { hideDragHint(); return; }
    const QPoint pos = e->position().toPoint();
    const bool valid = dropOnValidTarget(pos) && dropTargetMeaningful(pos, e);
    if (valid) {
        e->acceptProposedAction();
        updateDragHint(pos, true);              // 2026-09-02:光标旁"复制/移动"浮标
        updateFolderDropTarget(pos, true);      // 树落点白框
    } else {
        e->ignore();                            // 禁止光标 + 松开无动作
        hideDragHint();
        // 2026-09-04:禁止落点(同目录/被拖夹自身)仍保留树落点白框 —— 光标说
        // "放不进去",白框说"你悬停的是这一行"。落点在树外时不画(落空即清)。
        if (dropOnValidTarget(pos)) updateFolderDropTarget(pos, true);
    }
}

// 拖出窗口:Qt 补发 leave。清"复制/移动"浮标与树落点白框,防白框残留在
// 最后一行上(拖放全程没有真正的鼠标移动事件,不 leave 就一直挂着)。
void MainWindow::dragLeaveEvent(QDragLeaveEvent* e) {
    hideDragHint();
    QMainWindow::dragLeaveEvent(e);
}

void MainWindow::dropEvent(QDropEvent* e) {
    hideDragHint();
    const QMimeData* md = e->mimeData();
    if (!md || !md->hasUrls()) return;
    const QPoint gpos = e->position().toPoint();
    if (!dropOnValidTarget(gpos)) { e->ignore(); return; }
    // 2026-09-03 双保险:dragMove 阶段无效落点已 ignore 出禁止光标,dropEvent
    // 再兜一道 —— 网格空白/非文件夹卡片/树中源所在目录节点(自己移自己)一律
    // 不执行任何动作(不导航、不弹窗、不移动),彻底封死。外部拖入不受影响
    // (dropTargetMeaningful 对 source!=网格 直接放行)。
    if (!dropTargetMeaningful(gpos, e)) { e->ignore(); return; }

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
        const QString verb = copy ? gazeTr("复制") : gazeTr("移动");
        const QString what = paths.size() == 1
            ? QFileInfo(paths.first()).fileName()
            : gazeTr("%1 个项目").arg(paths.size());

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
                ? gazeTr("(松开 Ctrl 再拖即为移动)")
                : gazeTr("(按住 Ctrl 拖放即为复制)");
            if (QMessageBox::question(this, gazeTr("拖放%1").arg(verb),
                    gazeTr("将 %1 %2到\n%3 ?\n\n%4")
                        .arg(what, verb, dropIntoDir, tip),
                    QMessageBox::Yes | QMessageBox::No) != QMessageBox::Yes) {
                return;   // 用户取消:什么都不做
            }
        }

        if (!copy) releaseGazeFileLocks(actionable);   // #214:移动前放句柄(播放中视频/音频被锁)
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
            showDeleteToast(this, gazeTr("已%1 %2 项到目标文件夹").arg(verb).arg(done));
        }
        if (!errs.isEmpty())
            QMessageBox::warning(this,
                gazeTr("部分项目未能%1").arg(verb),
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
    // ── 全屏胶片条(#203):这里只驱动显隐;条上的点击/滚轮/拖动平移都由
    // FilmStrip 控件自己处理,不再走窗口级 eventFilter ──
    if (m_filmStrip && event->type() == QEvent::MouseMove) {
        auto* me = static_cast<QMouseEvent*>(event);
        // #208 修正:本过滤器是应用级的,obj 可以是任何子件,position() 是
        // **目标控件的局部坐标** —— 直接拿来判"是否在顶区"坐标系会随目标
        // 漂移。统一换算成窗口坐标;光标已落在条上时事件归条自理,不打扰
        const QPoint mp = mapFromGlobal(me->globalPosition().toPoint());
        if (!m_filmStrip->isVisible() || !m_filmStrip->geometry().contains(mp))
            updateFilmStrip(&mp);
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
    // ── #232(2026-09-04 用户令):Alt 裸键 = 死键 ──
    // 单独按 Alt 不得聚焦菜单栏、不得有任何动作(用户原话:"单独alt不要生效,
    // 不要去给我选菜单栏")。用户大量使用 Alt 相关键(Alt+Q 映射退格、Alt+Left
    // 导航),Qt 原生"裸 Alt=菜单栏"只添乱;菜单栏助记符已全部拆除
    // (mainwindow_menus.cpp)。实测(cache/tmp/probe_alt.cpp v3):只消费
    // KeyPress/KeyRelease 挡不住 —— 菜单栏 grabShortcut(Alt) 走 QShortcutMap,
    // 比 qApp 过滤器早一层;连 ShortcutOverride(Key_Alt) 一起消费才断得干净。
    // 组合键天然不受影响:ShortcutOverride 的 key() 是另一个键(Alt+Left 的
    // key()==Key_Left),Press/Release 又被 modifiers 闸挡住。Alt+退格(#155)、
    // Alt+Space(系统菜单)不落这两个分支,照旧。
    // 豁免:弹窗开着(菜单里的 Alt 助记符仍是活键)、对话框内(原生行为原样)。
    if (!QApplication::activePopupWidget()) {
        if (event->type() == QEvent::ShortcutOverride) {
            auto *ke = static_cast<QKeyEvent*>(event);
            auto *tw = qobject_cast<QWidget*>(obj);
            if (ke->key() == Qt::Key_Alt && !(tw && insideDialog(tw)))
                return true;
        } else if (event->type() == QEvent::KeyPress
                   || event->type() == QEvent::KeyRelease) {
            auto *ke = static_cast<QKeyEvent*>(event);
            auto *tw = qobject_cast<QWidget*>(obj);
            if (ke->key() == Qt::Key_Alt
                && ke->modifiers() == Qt::NoModifier
                && !ke->isAutoRepeat()
                && !(tw && insideDialog(tw)))
                return true;
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
        // #239(2026-09-04 用户令):地址栏空文本回车 = 死键,什么都不做。
        // 根因:QLineEdit 发出 returnPressed 后对回车事件 ignore,事件冒泡到
        // 主窗再进下方 Enter 路由 → requestSwitchMode → 给当前选中项(常是
        // 自动选中的第一项文件夹)开出查看器签,看起来就是"空栏回车开了个
        // 文件夹标签页"。文本非空时 gotoTypedPath 正常跳转且 m_lastAddrJumpMs
        // 宽限(#128②)吸收冒泡,无需这里管;只截空文本这一种。
        if (obj == m_addrBar
            && (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter)
            && ke->modifiers() == Qt::NoModifier) {
            QString raw = m_addrBar->text().trimmed();
            while (raw.size() >= 2 && raw.startsWith('"') && raw.endsWith('"'))
                raw = raw.mid(1, raw.size() - 2).trimmed();
            if (raw.isEmpty()) return true;
        }
        // 路由只看"这个键送给了谁",不看 QApplication::focusWidget():焦点在别处、
        // 键却发给弹窗的情况(QMenu/下拉列表都不是 QDialog)旧写法会整段吞掉 Enter/Esc。
        // main.cpp 的对话框过滤器装得更早、先触发,这里是第二道闸。
        const bool bypass = !tgt
            || ke->isAutoRepeat()                    // 按住不放:不该反复刷标记、反复起幻灯片
            || insideDialog(tgt)
            || QApplication::activePopupWidget()     // 有弹窗开着,键盘归弹窗
            // 查看器的键归查看器表:默认表里"适应窗口"就是 F，而浏览器 F=红标，
            // 不让路的话设置页那张表配了什么都会被浏览器键位吞掉。
            // #234 例外:"全屏预览"虽在表里,执行端是主窗(toggleFullView 不归面板),
            // 不让路 —— 否则查看器态按 G 被让走后没人执行
            || (m_viewerMode && m_preview->claimsHotkey(ke)
                && !m_preview->triggersAction(ke, "全屏预览"));
        if (!bypass) {
            // 键盘就是这些控件的输入手段,字母/Space/Enter/Esc 一律不抢
            const bool forText = textInputWidget(tgt);
            // 浏览器态媒体键(2026-09-03):预览面板正在显示媒体(视频/音频/GIF)
            // 时,查看器表的"播放/暂停""停止"(默认 T)在浏览器同样生效 —— 此前
            // T 只在查看器模式作数,浏览器选中视频、预览自动播放后按 T 无反应。
            // 默认键位不与浏览器字母键(F/D/G)撞车;用户自定义改键相撞时媒体态优先。
            if (!forText && !m_viewerMode && m_preview->handleBrowserMediaKey(ke))
                return true;
            // Space/Enter 在按钮/列表/滑块里有本职动作(激活、选中、就地编辑)
            const bool forActivation = forText || activationKeyWidget(tgt);
            // 空格例外(2026-09-03 用户令):焦点落在文件树/文件页/滑块等任何
            // 位置,空格仍是预览媒体播放/暂停 —— 条目视图"空格选中当前项"的
            // 本职让位。仅文本输入(打空格)与按钮(激活)保留空格本职。
            const bool spaceReserved = forText
                || qobject_cast<const QAbstractButton*>(tgt);
            // Keyboard/space:0 播放/暂停(默认) 1 什么都不做 2 下一个文件 3 快速幻灯片
            // (选项表与设置→快捷键→空格一致)
            // 只拦不带 Ctrl/Alt/Meta 的空格:Alt+Space 是系统窗口菜单
            if (!spaceReserved
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
                // #220(2026-09-04 用户令):G 全屏预览里 C/V/方向键 = 滚轮(上一个/
                // 下一个)。网格被藏起、焦点不在它身上,FileGrid::keyPressEvent 收
                // 不到这些键;查看器形态的 Left/Right 早在 bypass 处被查看器热键表
                // 放行,这里补的是浏览器形态全屏的缺口。设置页"方向键=滚动"在此
                // 不适用:全屏没有网格可滚,键位一律导航(与滚轮同一条链)。
                if (m_fullView && ke->modifiers() == Qt::NoModifier) {
                    switch (ke->key()) {
                    case Qt::Key_C: case Qt::Key_Left: case Qt::Key_Up:
                        m_fileGrid->navigateSelection(-1); return true;
                    case Qt::Key_V: case Qt::Key_Right: case Qt::Key_Down:
                        m_fileGrid->navigateSelection(1); return true;
                    default: break;
                    }
                }
                // 颜色标记快捷键:Ctrl+0~5(0=取消)/ F=红 / D=取消;Ctrl+PgUp/PgDn 快退快进
                if (ke->modifiers() & Qt::ControlModifier) {
                    if (ke->key() >= Qt::Key_0 && ke->key() <= Qt::Key_5) {
                        applyColorLabel(ke->key() - Qt::Key_0);
                        return true;
                    }
                    // #221(2026-09-04 用户令):Ctrl+PgUp/PgDn = 切换左/右标签页
                    // (顶替原快退/快进,seek 挪 Shift+PgUp/PgDn)。#228(同日用户令):
                    // 预览正在放视频/音频("视频页")时这对键改回快退/快进 —— 进度
                    // 条调整优先于切签,图片页维持切签。到头钳住不回绕;
                    // G 全屏里标签条收着、没有可切的样子,落回下面的快退快进。
                    // 切到「浏览器」标签时 currentChanged 自己会退回浏览器(#105)
                    if (m_viewerTabs && !m_fullView && m_viewerTabs->count() > 1
                        && (ke->modifiers() & Qt::ShiftModifier) == 0
                        && !m_preview->showingMedia()
                        && (ke->key() == Qt::Key_PageUp || ke->key() == Qt::Key_PageDown)) {
                        const int dir = ke->key() == Qt::Key_PageUp ? -1 : 1;
                        const int nxt = qBound(0, m_viewerTabs->currentIndex() + dir,
                                               m_viewerTabs->count() - 1);
                        if (nxt != m_viewerTabs->currentIndex())
                            m_viewerTabs->setCurrentIndex(nxt);
                        return true;
                    }
                    // Viewer/seekSeconds:一次跳多少秒(设置→键盘;默认 3)
                    if (ke->key() == Qt::Key_PageUp) {
                        m_preview->seekDelta(-seekSeconds()); return true;
                    }
                    if (ke->key() == Qt::Key_PageDown) {
                        m_preview->seekDelta(seekSeconds()); return true;
                    }
                    // #221:Ctrl+Shift+T = 恢复最近关掉的文件标签
                    // (与 Ctrl+W/双击关签配套;查看器热键表的单键 T=停止不冲突)
                    if (ke->key() == Qt::Key_T && (ke->modifiers() & Qt::ShiftModifier)) {
                        restoreClosedViewerTab(); return true;
                    }
                    // Ctrl+W = 关闭当前标签卡(2026-09-01 用户令)。落在「浏览器」
                    // 标签或浏览器模式时没有可关的内容标签,按键落空 —— 浏览器
                    // 标签是回标准模式的出口,不是内容;关到最后一张图片标签时
                    // closeViewerTab 自己会退回浏览器。
                    // #230:全屏里 Ctrl+W 只退全屏、不接着关签 —— 与双击同语义
                    // (#227 只关最上一层),想关签退全屏后再按
                    if (ke->key() == Qt::Key_W && m_viewerMode && m_viewerTabs) {
                        if (m_fullView) { exitFullView(); return true; }
                        if (!isBrowserTab(m_viewerTabs->currentIndex())) {
                            closeViewerTab(m_viewerTabs->currentIndex());
                            return true;
                        }
                    }
                } else if (ke->modifiers() == Qt::ShiftModifier) {
                    // #221:快退/快进自 Ctrl+PgUp/PgDn 挪来(那对键改切标签页),
                    // 秒数仍是 Viewer/seekSeconds。查看器热键表没有 PgUp/PgDn,
                    // 查看器态一样走到这;全屏里也可用。
                    if (ke->key() == Qt::Key_PageUp) {
                        m_preview->seekDelta(-seekSeconds()); return true;
                    }
                    if (ke->key() == Qt::Key_PageDown) {
                        m_preview->seekDelta(seekSeconds()); return true;
                    }
                } else if (ke->modifiers() == Qt::NoModifier) {
                    if (ke->key() == Qt::Key_F) { applyColorLabel(1); return true; }
                    if (ke->key() == Qt::Key_D) { applyColorLabel(0); return true; }
                    // G=全屏预览(#154):直接铺满只留画面,不进查看器不碰标签;
                    // 再按 G/ESC 完全回到按 G 前的布局。走这道过滤器而不是菜单
                    // QAction 的 shortcut:上面那几层 forText/弹窗判断才是"裸键
                    // 不该抢文本框"的防线(#61)。
                    // #234:键位改由查看器热键表驱动(ViewerShortcut/全屏预览,默认
                    // G)——设置→快捷键可改,主窗/预览右键菜单右列跟着同一张表
                    if (m_preview->triggersAction(ke, "全屏预览")) {
                        toggleFullView(); return true;
                    }
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
