#include "mainwindow.h"
#include "foldertree.h"
#include "filegrid.h"
#include "views/filmstrip.h"   // #203:onSelectionChanged 里让胶片条跟着切图
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
#include <QDirIterator>
#include <QElapsedTimer>
#include <QThreadPool>

#include "mainwindow_internal.h"

// 树点击导航的入口包装:先把"来源=文件树"记下来再进 navigateTo,之后复位。
// navigateTo 底部的 setFocus 就不再加到网格头上 —— 焦点留在树上,树亮蓝、
// 网格暗蓝,正是"选中色随焦点分流"想要的两套状态。
void MainWindow::onTreeFolderSelected(const QString& path) {
    // 点的正是当前所在目录:跳过重扫 —— loadDirectory 会把选中项重置到第一排
    // 第一个,并把刚铺好的滚动/预览打断。点击本身已让树得焦,文件页选中色随
    // 失焦变暗(两档蓝分流),正是用户要的「保留原选中、仅变暗」。
    if (mw_impl::canonicalPath(path).compare(m_currentDir, Qt::CaseInsensitive) == 0)
        return;
    m_navFromTree = true;
    navigateTo(path);
    m_navFromTree = false;
}

// 注:MainWindow::onGridDirSelected(文件页鼠标单选目录卡 → 文件树镜像选中)
// 已于 2026-09-03 按用户裁决移除。用户原话:「当文件页中选中某文件夹时,文件树
// 应当依然在原处……只有我双击打开该文件夹时,你才该去选中它。」
// 于是树同步收口到唯一一处:navigateTo(见其末尾的 focusPath),即只有真正
// 进入目录(双击目录卡/地址栏回车/前进后退/上级/树点击)才移动树。

bool MainWindow::navigateTo(const QString &path) {
    QString p = mw_impl::canonicalPath(path);
    if (p == "..") {
        QFileInfo fi(m_currentDir);
        p = mw_impl::canonicalPath(fi.dir().absolutePath());
    }
    if (!QFileInfo::exists(p) || !QFileInfo(p).isDir()) return false;

    // 目录导航历史:新跳转截断前进分支后入栈;历史回跳(m_histNav)不入栈
    if (!m_histNav) {
        while (m_history.size() > m_histIdx + 1) m_history.removeLast();
        if (m_history.isEmpty() || m_history.last() != p)
            m_history.append(p);
        m_histIdx = m_history.size() - 1;
        updateNavEnabled();       // 新跳一定截断了前进分支 → 前进该立刻变灰
    }

    m_currentDir = p;              // 内部规范形:标题模板/上级跳转/刷新都读这里
    m_addrBar->setText(mw_impl::displayPath(p));   // 显示:反斜杠 + 末尾 "\"
    m_currentFile.clear();
    // lastDir 的唯一读者是下次启动 → setPersist 不广播(广播会重排网格/重绘预览)。
    // 判等仍留着:F5 与重复进同一目录会走这里,别白脏一次 ini
    AppSettings& st = AppSettings::instance();
    if (st.get("Browser/lastDir", QString()).toString() != p)
        st.setPersist("Browser/lastDir", p);   // Start/withoutFile=上次目录 读回
    applyTitle();

    // 历史访问路径记录(去重置顶,上限 30;地址栏下拉用)
    {
        QSettings s = mw_impl::appSettings();
        QStringList lst = s.value("Browser/pathHistory").toStringList();
        lst.removeAll(p);
        lst.prepend(p);
        while (lst.size() > 30) lst.removeLast();
        s.setValue("Browser/pathHistory", lst);
    }

    m_fileGrid->loadDirectory(p);
    // 焦点必须跟着落到网格:loadDirectory 只做了"自动选中第一项",不碰焦点。
    // 从地址栏/历史/双击卡片以外进门(尤其点过地址栏↑按钮)时,焦点会留在
    // QAbstractButton 上,而空格对按钮的本职就是点击它 —— 表现成
    // "进文件夹后按空格回到了上一级"。模态对话框在场时不抢它的焦点。
    // 例外:本人就是文件树点击触发(树自己会取焦点,网格不该自认为是操作对象),
    // 否则焦点被抢回后,网格一直亮蓝、树永远暗蓝,两边分流失效。
    if (!m_navFromTree && !QApplication::activeModalWidget()) m_fileGrid->setFocus();
    // 不 clear():loadDirectory 内部已默认选中第一项并触发预览加载,
    // 这里再 clear 会把刚发起的预览抹掉(进文件夹预览空白的原因)。
    // 空目录时 selectionChanged({}) 自行走 clear,无需代办
    // 树跟随当前目录 —— 全项目唯一的树同步落点(2026-09-03 用户裁决后收口):
    // 只有**真正进入**目录才动树,即双击目录卡 / 地址栏回车 / 前进后退 / 上级
    // / 搜索定位 / 树点击(本函数)这几条路。文件页里单击(或键盘)选中一个
    // 文件夹卡片时树必须留在原处,不再镜像跟随。
    if (m_folderTree) m_folderTree->focusPath(p);
    return true;
}

// ── 地址栏回车(#109②):"跳转该路径"对目录和文件都要有下文 ──
// 旧写法把文本直接递给 navigateTo,而它第一行就是 !isDir → return false:
// 从资源管理器"复制文件地址"粘一条 .jpg 进来,按回车什么都不发生,
// 看上去就像回车被别的快捷键吃了。三档各自要有可见结果,不许静默:
//   目录   → 进这个目录
//   文件   → 进它的目录并选中它(预览跟着出来)
//   都不像 → 状态栏写明白按的是哪条路径
// 粘贴内容常带引号(Explorer 的"复制文件地址"就是带引号的),先脱掉再判。
void MainWindow::gotoTypedPath() {
    QString raw = m_addrBar->text().trimmed();
    while (raw.size() >= 2 && raw.startsWith('"') && raw.endsWith('"'))
        raw = raw.mid(1, raw.size() - 2).trimmed();
    if (raw.isEmpty()) return;
    m_lastAddrJumpMs = QDateTime::currentMSecsSinceEpoch();   // #128②:见 Enter 宽限
    if (navigateTo(raw)) return;
    const QFileInfo fi(mw_impl::canonicalPath(raw));
    if (fi.isFile()) { revealFile(fi.absoluteFilePath()); return; }
    Logger::event(QStringLiteral("addr: cannot jump to '%1'").arg(raw));
    m_statusLabel->setText(gazeTr("路径不存在: %1")
                               .arg(QDir::toNativeSeparators(raw)));
}

// #125 「自定义(扩展名…)」筛选:选中这一档 = 编辑自己的扩展名清单并立刻生效。
// 清单存在 ini Browser/customExts(逗号分隔、不带点、小写),真正的匹配在
// FileGrid::applyFilter 里做。**启动恢复不弹这个框**:filterMode 落盘为
// FILTER_CUSTOM 时 FileGrid 直接按存量清单筛,免得每次开机都要点一次取消。
void MainWindow::editCustomFilter() {
    AppSettings& st = AppSettings::instance();
    bool ok = false;
    const QString cur = st.get("Browser/customExts", QString()).toString();
    const QString txt = QInputDialog::getText(
        this, gazeTr("自定义格式筛选"),
        gazeTr("只显示这些扩展名的文件(逗号分隔,不用写点):\n"
                          "例:psd, ai, raw, cr2, nef"),
        QLineEdit::Normal, cur, &ok).trimmed();
    if (!ok) {   // 取消:筛选维持原样,指示器也弹回去(下拉框刚才已停在"自定义…")
        syncFilterIndicators(m_fileGrid->filterMode());
        return;
    }
    QStringList parts;
    for (const QString& a : txt.split(QLatin1Char(','), Qt::SkipEmptyParts))
        for (const QString& b : a.split(QLatin1Char(';'), Qt::SkipEmptyParts))
            for (const QString& c : b.split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
                QString e = c.trimmed().toLower();
                while (e.startsWith('.')) e.remove(0, 1);
                if (!e.isEmpty() && !parts.contains(e)) parts << e;
            }
    st.set("Browser/customExts", parts.join(','));
    if (parts.isEmpty()) {
        // 清单空 = 这一档不该"看起来生效却什么都不显示":退回全部并说明
        m_fileGrid->setFilterMode(FILTER_ALL);
        m_statusLabel->setText(
            gazeTr("自定义筛选的扩展名清单是空的,已回到\"全部\""));
        return;
    }
    m_fileGrid->setFilterMode(FILTER_CUSTOM);   // 换档后重筛当前目录
    m_statusLabel->setText(gazeTr("自定义筛选:%1 项扩展名(%2)")
                               .arg(parts.size()).arg(parts.join(',')));
}

void MainWindow::updateStatus() {
    int fc = m_fileGrid->fileCount();
    int sc = m_fileGrid->selectedCount();
    qint64 ss = m_fileGrid->selectedSize();
    QString text = gazeTr("%1 项").arg(fc);
    // #241(2026-09-04 用户令):单选文件夹的大小不再给「≈」约值 —— 约等于易误导。
    // 后台线程全量递归,过程显示「统计中…」+ 已累计字节(数字随统计推进越来越大),
    // 算完即精确值。同目录已有精确值则直接用(会话内缓存,切换回来不重算)。
    QString sizeText;
    if (sc == 1) {
        const auto paths = m_fileGrid->selectedPaths();
        if (!paths.isEmpty() && QFileInfo(paths.first()).isDir()) {
            const QString dp = paths.first();
            if (m_dirSizeDone && m_dirSizeTarget == dp) {
                ss = m_dirSizeValue;
                sizeText = formatSize(ss);
            } else {
                if (m_dirSizeRunning && m_dirSizeTarget == dp) {
                    ss = m_dirSizePartial;
                } else {
                    startDirSizeRun(dp);   // 首次见到该目录:发起后台统计
                    ss = 0;
                }
                sizeText = ss > 0 ? gazeTr("统计中… %1").arg(formatSize(ss))
                                  : gazeTr("统计中…");
            }
        } else if (m_dirSizeRunning) {
            cancelDirSizeRun();   // 选中项不是(单个)目录:停掉旧统计
        }
    } else if (m_dirSizeRunning) {
        cancelDirSizeRun();
    }
    if (sc > 0) {
        text += gazeTr("  ·  已选 %1 项 · [%2]")
                    .arg(sc)
                    .arg(sizeText.isEmpty() ? formatSize(ss) : sizeText);
        auto paths = m_fileGrid->selectedPaths();
        if (!paths.isEmpty()) {
            QFileInfo fi(paths.first());
            const QString oneSize = (fi.isDir() && sc == 1)
                                  ? sizeText
                                  : formatSize(fi.size());
            text += "  " + fi.fileName()
                  + "  " + oneSize
                  + "  " + fi.lastModified().toString("yyyy/M/d - HH:mm:ss");
        }
    }
    m_statusLabel->setText(text);
    m_pathLabel->setText(m_addrBar->text());
}

// ── #241 文件夹大小后台精确统计 ──
// 旧 quickDirSize 只有 30ms 时间片,超了就截断报「≈」约值;现把递归整体挪进
// 线程池:GUI 零阻塞,每 ~24ms 上报一次累计值,状态栏「统计中… x GB」随之增长,
// 算完出精确值。协同取消:选择一变 stop 旗置位,旧线程下一轮循环即弃;
// 迟到的中途/收尾上报按 runId 作废。
void MainWindow::startDirSizeRun(const QString& path) {
    if (m_dirSizeRunning) m_dirSizeStop->store(true);   // 取消旧一轮
    m_dirSizeStop = std::make_shared<std::atomic_bool>(false);
    auto stop = m_dirSizeStop;
    const quint64 runId = ++m_dirSizeRunId;
    m_dirSizeRunning = true;
    m_dirSizeDone    = false;
    m_dirSizeTarget  = path;
    m_dirSizePartial = 0;
    QPointer<MainWindow> self(this);
    QThreadPool::globalInstance()->start([self, path, stop, runId]() {
        qint64 sz = 0;
        QElapsedTimer t; t.start();
        qint64 lastPost = 0;
        bool stopped = false;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (stop->load()) { stopped = true; break; }
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isFile()) sz += fi.size();
            if (t.elapsed() - lastPost >= 24) {   // 状态栏滚动的更新节奏
                lastPost = t.elapsed();
                QMetaObject::invokeMethod(self, [self, path, runId, sz]() {
                    if (self) self->applyDirSizeProgress(path, runId, sz);
                }, Qt::QueuedConnection);
            }
        }
        QMetaObject::invokeMethod(self, [self, path, runId, sz, stopped]() {
            if (self && !stopped) self->applyDirSizeDone(path, runId, sz);
        }, Qt::QueuedConnection);
    });
}

void MainWindow::cancelDirSizeRun() {
    if (!m_dirSizeRunning) return;
    m_dirSizeStop->store(true);   // 线程下一轮循环即弃
    ++m_dirSizeRunId;             // 迟到的上报一律作废
    m_dirSizeRunning = false;
}

void MainWindow::applyDirSizeProgress(const QString& path, quint64 runId,
                                      qint64 bytes) {
    if (runId != m_dirSizeRunId || !m_dirSizeRunning || m_dirSizeTarget != path)
        return;
    m_dirSizePartial = bytes;
    updateStatus();   // 用新累计值重排状态栏(「统计中… x GB」)
}

void MainWindow::applyDirSizeDone(const QString& path, quint64 runId,
                                  qint64 bytes) {
    if (runId != m_dirSizeRunId || !m_dirSizeRunning || m_dirSizeTarget != path)
        return;
    m_dirSizeRunning = false;
    m_dirSizeDone    = true;
    m_dirSizeValue   = bytes;
    updateStatus();
}

void MainWindow::onSelectionChanged(const QString &path) {
    m_currentFile = path;
    if (m_slideshow && path.isEmpty()) toggleSlideshow();
    m_preview->loadFile(path);
    // 导航不新增标签,但要让标签条跟着显示这张(已是当前标签则原地不动)
    if (m_viewerMode && !path.isEmpty()) syncViewerTab(path);
    // #203:G 全屏预览开着胶片条时,方向键/列表切图让条的蓝框/居中/题注跟过来
    if (m_fullView && m_filmStrip && m_filmStrip->isVisible())
        m_filmStrip->syncCurrent(path);
    // 相邻预读:方向键切下一张/上一张时零等待显示
    m_preview->preload(m_fileGrid->neighborOf(path, -1),
                       m_fileGrid->neighborOf(path, +1));
    addRecentFile(path);   // 预览显示过即记入"最近的文件"(上限 100)
    // #80:信息面板开着才喂数据(关着时 showFile 会自己短路,不排后台任务)
    if (m_info) m_info->showFile(path);
    updateStatus();
    // Start/rememberFilename:记住最后选中的文件,下次启动定位回去
    // 该键没有运行期读者 → setPersist 不广播:实测一次广播要 4.2~4.8 ms
    // (网格重排 + 标题重算 + 预览重绘),方向键连按时全是白付
    if (!path.isEmpty()
        && AppSettings::instance().get("Start/rememberFilename", true).toBool()
        && AppSettings::instance().get("Browser/lastFile", QString()).toString() != path)
        AppSettings::instance().setPersist("Browser/lastFile", path);
    applyTitle();
}

void MainWindow::onSizeChanged(int value) {
    m_fileGrid->setCardSize(value);
}

// Fullscreen/dualMonitor:开且有第二块屏时,全屏窗口落到第二屏(默认关=当前屏)
void MainWindow::enterFullscreen() {
    if (AppSettings::instance().get("Fullscreen/dualMonitor", false).toBool()) {
        const QList<QScreen*> screens = QApplication::screens();
        if (screens.size() > 1) {
            QScreen* second = screens[1];
            // 先把窗口搬到目标屏(保持当前尺寸),再全屏 —— 直接 showFullScreen
            // 会在窗口当前所在的屏上展开
            setGeometry(QRect(second->geometry().topLeft(), size()));
        }
    }
    showFullScreen();
}

void MainWindow::openFullscreen(const QString& path) {
    QFileInfo fi(path);
    if (fi.exists()) {
        navigateTo(fi.absolutePath());
        m_fileGrid->selectByPath(path);
    }
    enterFullscreen();
}

// 以文搜图结果定位:导航到所在目录并选中该文件(窗口前置)
void MainWindow::revealFile(const QString& path) {
    QFileInfo fi(path);
    if (!fi.exists()) return;
    // 目录没变就别 navigateTo:那会重扫整目录并把旧选中项再解一遍
    // (实测交接日志里 loadFile 134 紧跟着 loadFile 目标 = 两遍解码)
    if (fi.absolutePath() != m_currentDir) navigateTo(fi.absolutePath());
    m_fileGrid->selectByPath(path);
    activateWindow();
    raise();
}

// 标题模板求值:
//   {路径} {文件夹} {文件夹名} {文件名} {文件名 含扩展名} {大小} {创建日期} {修改日期}
//   {颜色标签} {宽} {高}
//   时间变量  大写=修改时间 {Y}{M}{D}{H}{N}{S},小写=创建时间 {y}{m}{d}{h}{n}{s},
//             组合 {Y-m-d_H-N-S} / {Y_m_d_H_N_S}
QString MainWindow::renderTitle(const QString& tplIn, const QString& filePath) const
{
    QString tpl = tplIn.trimmed().isEmpty()
        ? QString::fromUtf8("{路径} - Gaze") : tplIn;

    QFileInfo fi(filePath);
    const bool hasSel   = !filePath.isEmpty() && fi.exists();
    const bool isDirSel = hasSel && fi.isDir();
    const bool hasFile  = hasSel && !isDirSel;
    // 选中目录时它就在当前目录里,fi.absolutePath() 即父目录:
    // {文件夹} 仍指向"所在的文件夹",和文件那一侧语义对齐
    const QString dir  = hasSel ? fi.absolutePath() : m_currentDir;
    const QString dirName = dir.section(QLatin1Char('/'), -1);

    QString sizeText, mdate, cdate, label;
    int w = 0, h = 0;
    // 日期与颜色标签对目录同样有效;体积要递归扫描才知道,给 0 等于说谎,故留空
    if (hasSel) {
        mdate = fi.lastModified().toString("yyyy/MM/dd - HH:mm:ss");
        cdate = fi.birthTime().toString("yyyy/MM/dd - HH:mm:ss");
        int cl = m_fileGrid->colorLabelOf(filePath);
        static const char* names[] = {"", "红", "橙", "黄", "绿", "蓝"};
        if (cl >= 1 && cl <= 5) label = gazeTr(names[cl]);
    }
    if (hasFile) {
        const bool bytes = AppSettings::instance()
            .get("FileList/sizeInBytes", false).toBool();
        sizeText = bytes ? QString::number(fi.size()) + " B" : formatSize(fi.size());
        QSize sz = imageSize(fi.absoluteFilePath());
        w = sz.width(); h = sz.height();
    }

    auto subst = [&tpl](const QString& from, const QString& to) {
        const QString tok = QLatin1Char('{') + from + QLatin1Char('}');
        if (!to.isEmpty()) { tpl.replace(tok, to); return; }
        // 值为空:连同紧邻的一个 " - " 分隔符一起吞掉,避免标题里残留 "A -  - B"
        // 优先吞后置分隔符(前有内容时),否则吞前置分隔符(前无内容时)
        int at = tpl.indexOf(tok);
        while (at >= 0) {
            if (tpl.mid(at + tok.size()).startsWith(QStringLiteral(" - ")))
                tpl.remove(at, tok.size() + 3);
            else if (at >= 3 && tpl.mid(at - 3, 3) == QStringLiteral(" - "))
                tpl.remove(at - 3, 3 + tok.size());
            else
                tpl.remove(at, tok.size());
            at = tpl.indexOf(tok);
        }
    };
    // 组合时间式先替换(避免被单字母规则拆碎)
    const QDateTime modT  = hasSel ? fi.lastModified() : QDateTime();
    const QDateTime birthT = hasSel ? fi.birthTime() : QDateTime();
    auto fmtT = [](const QDateTime& t, const QString& sep) {
        if (!t.isValid()) return QString();
        return t.toString("yyyy" + sep + "MM" + sep + "dd") + "_"
             + t.toString("HH-mm-ss");
    };
    subst("Y-m-d_H-N-S", fmtT(modT,  "-"));
    subst("Y_m_d_H_N_S", fmtT(modT,  "_"));
    subst("y-m-d_h-n-s", fmtT(birthT, "-"));
    subst("y_m_d_h_n_s", fmtT(birthT, "_"));
    // 标题里的路径同样用反斜杠(与地址栏一致);末尾 "\" 只属于可编辑的地址栏
    subst(gazeTr("路径"),
          QDir::toNativeSeparators(hasSel ? fi.absoluteFilePath() : dir));
    subst(gazeTr("文件夹"), QDir::toNativeSeparators(dir));
    subst(gazeTr("文件夹名"), dirName);
    // 目录没有"扩展名"这一说:两个名字令牌都给完整目录名,
    // 不能套 completeBaseName 的点切分(那会把 "A.B 文件夹" 截成 "A")
    subst(gazeTr("文件名"),
          isDirSel ? fi.fileName() : (hasFile ? fi.completeBaseName() : QString()));
    subst(gazeTr("文件名 含扩展名"), hasSel ? fi.fileName() : QString());
    subst(gazeTr("大小"), sizeText);
    subst(gazeTr("修改日期"), mdate);
    subst(gazeTr("创建日期"), cdate);
    subst(gazeTr("颜色标签"), label);
    subst(gazeTr("宽"), w > 0 ? QString::number(w) : QString());
    subst(gazeTr("高"), h > 0 ? QString::number(h) : QString());
    // 单字母时间变量:大写=修改时间,小写=创建时间(N/n=分钟,与 M/m=月 区分)
    auto part = [](const QDateTime& t, QChar which) -> QString {
        if (!t.isValid()) return QString();
        switch (which.toUpper().toLatin1()) {
        case 'Y': return t.toString("yyyy");
        case 'M': return t.toString("MM");
        case 'D': return t.toString("dd");
        case 'H': return t.toString("HH");
        case 'N': return t.toString("mm");
        case 'S': return t.toString("ss");
        }
        return QString();
    };
    for (const QPair<QChar, QDateTime>& p :
         { qMakePair(QChar('Y'), modT), qMakePair(QChar('M'), modT),
           qMakePair(QChar('D'), modT), qMakePair(QChar('H'), modT),
           qMakePair(QChar('N'), modT), qMakePair(QChar('S'), modT),
           qMakePair(QChar('y'), birthT), qMakePair(QChar('m'), birthT),
           qMakePair(QChar('d'), birthT), qMakePair(QChar('h'), birthT),
           qMakePair(QChar('n'), birthT), qMakePair(QChar('s'), birthT) }) {
        subst(QString(p.first), part(p.second, p.first));
    }
    return tpl;
}

void MainWindow::applyTitle() {
    const QString tpl = AppSettings::instance().get(
        m_viewerMode ? "Interface/titleViewer" : "Interface/titleBrowser",
        QString::fromUtf8("{文件夹} - {文件名 含扩展名} - Gaze")).toString();
    setWindowTitle(renderTitle(tpl, m_currentFile));
}

// 历史到头就要看得见地"到头":菜单项和工具栏按钮一起灰，
// Alt+←/→ 跟着 action 一起失效(不留"按了没反应"的隐形快捷键)
void MainWindow::updateNavEnabled() {
    const bool canBack = m_histIdx > 0;
    const bool canFwd  = m_histIdx >= 0 && m_histIdx + 1 < m_history.size();
    if (m_actBack) m_actBack->setEnabled(canBack);
    if (m_actFwd)  m_actFwd->setEnabled(canFwd);
    if (m_btnBack) m_btnBack->setEnabled(canBack);
    if (m_btnFwd)  m_btnFwd->setEnabled(canFwd);
}

void MainWindow::goBack() {
    // 游标只在跳转真的发生后才动:目标目录可能已被删掉(删除就在本应用里做),
    // 旧写法先 --m_histIdx 再 navigateTo，早退时索引已提交，
    // 于是"看到的目录"和"游标指的条目"从此错位，之后每步前退都跟着错。
    // 记下要离开的目录:目的地若是它的父目录,跳成后定位选中它(2026-09-01
    // 用户令:后退到上一级=选中刚离开的子文件夹,不是滚回顶部)
    const QString leftDir = (m_histIdx >= 0 && m_histIdx < m_history.size())
                            ? m_history.at(m_histIdx) : QString();
    while (m_histIdx > 0) {
        const int cand = m_histIdx - 1;
        m_histNav = true;
        const bool ok = navigateTo(m_history[cand]);
        m_histNav = false;
        if (ok) { m_histIdx = cand; break; }
        m_history.removeAt(cand);          // 死条目摘掉，继续往前找，别卡死在这
        if (cand < m_histIdx) --m_histIdx; // 删的是游标之前的项，游标要跟着左移
    }
    updateNavEnabled();   // 历史跳转在 m_histNav 下跳过 navigateTo 里那次刷新
    // 只有"目的地 == 刚离开目录的父目录"才算退到上一级;斜着跳的历史步不定位
    if (!leftDir.isEmpty() && m_histIdx >= 0
        && QDir::cleanPath(QFileInfo(leftDir).absolutePath())
           == QDir::cleanPath(m_currentDir))
        m_fileGrid->selectByPath(leftDir);
}

void MainWindow::goForward() {
    while (m_histIdx + 1 < m_history.size()) {
        const int cand = m_histIdx + 1;
        m_histNav = true;
        const bool ok = navigateTo(m_history[cand]);
        m_histNav = false;
        if (ok) { m_histIdx = cand; break; }
        m_history.removeAt(cand);          // 删的是游标之后的项，游标不动
    }
    updateNavEnabled();
}

void MainWindow::goUp() {
    // 上级目录:跳成后定位刚离开的子文件夹(资源管理器"向上"同款,同 2026-09-01 用户令)
    const QString from = m_currentDir;
    if (navigateTo(QStringLiteral("..")))
        m_fileGrid->selectByPath(from);
}

void MainWindow::refresh() {
    if (m_currentDir.isEmpty()) return;
    // 刷新不是新跳转:走 m_histNav 那条不入栈的通道。
    // 旧写法按普通跳转走，"截断前进分支"照跑一遍 ——
    // 在后退过的位置上按一下 F5，前进那一支就没了。
    m_histNav = true;
    navigateTo(m_currentDir);
    m_histNav = false;
}

void MainWindow::onThumbZoom(int delta) {
    // 放大/缩小缩略图:基于当前尺寸步进 20px(区间由 setCardSize 统一收口)
    m_fileGrid->setCardSize(m_fileGrid->cardSizeValue() + delta * 20);
}

// 筛选指示器总同步(#107):格式下拉框、红标三态钮背景、m_redFilterMode 全部
// 单向跟着 FileGrid::filterMode 走 —— 菜单/下拉框/红钮/启动恢复哪个入口改筛选
// 都汇到这里,指示器不可能再脱钩。筛选持久化在 filegrid.cpp setFilterMode 里。
void MainWindow::syncFilterIndicators(int mode) {
    m_redFilterMode = (mode == FILTER_RED) ? 1 : (mode == FILTER_UNRED) ? 2 : 0;
    if (m_formatFilterCombo) {
        QComboBox* cb = m_formatFilterCombo;
        cb->blockSignals(true);
        int idx = -1;
        for (int i = 0; i < cb->count(); ++i)
            if (cb->itemData(i).toInt() == mode) { idx = i; break; }
        if (idx >= 0) {
            // 先定位再清占位:index 还是 -1 时清空占位文本会把 index 顶回 0
            cb->setCurrentIndex(idx);
            cb->setPlaceholderText(QString());
        } else {
            cb->setCurrentIndex(-1);
            cb->setPlaceholderText(gazeTr("筛选：") + mw_impl::filterModeName(mode));
        }
        cb->blockSignals(false);
    }
    if (m_redBtn)
        m_redBtn->setStyleSheet(m_redFilterMode == 0 ? "" :
            "QToolButton{background:#3B82F6;border-radius:4px;}");
}
