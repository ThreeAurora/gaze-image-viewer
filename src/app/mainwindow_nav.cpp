#include "thumbnailer_internal.h"   // 必须最先:#244 threadDb+dirsize 表;它带 windows.h,
                                    // 若晚于 fileentry.h(_WIN32_IE 钉 0x0600)进来,
                                    // shobjidl 的 SHCreateItemFromParsingName 声明就被
                                    // IE70 版本门挡掉(thumbnailer*.cpp 不含 fileentry 故无此病)
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
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QSqlError>
#include <QDateTime>
#include "thumbnailer_internal.h"   // #244:th_impl::threadDb(每线程连接)+dirsize 表

// ── #244 文件夹大小缓存库(thumbnails.db 的 dirsize 表) ──
// 与 #241 后台精确统计配套:库里命中且失效键未变 → 状态栏瞬间出精确值,
// 不再每次选中都「统计中」;深层文件变动(不动父目录 mtime 的)由 10 分钟
// 的静默重校验兜底 —— 先显旧值,算完悄悄替换。失效键=目录 mtime|直接子项
// 数|直接子项字节和:Windows 下直接子项增删/改名/变尺寸都会打翻它,是比
// 单看 mtime 强一档的"便宜哨兵";只列一层,成本与递归差一个量级。
// 单 TU 私有 helper(依 thumbnailer_internal.h 的规矩不进共享头)。
namespace {

constexpr qint64 kDirSizeRevalidateMs = 10 * 60 * 1000;   // 10 分钟强制复核

// 父目录 mtime 的单次 stat —— 失效键的"快筛"段。NTFS 下直接子项增删/改名
// 都会翻新父目录 mtime,所以 mtime 没变(绝大多数选中场景)就不必做下面的
// 全目录枚举比对:过去每次选中都 entryInfoList+逐项 stat 一遍,大目录一选
// GUI 线程就卡、磁盘就响(用户报"点个文件夹都咯吱咯吱"),先比这一段再决定
// 要不要贵的那步。
QString dirSizeMtimeStamp(const QString& dirPath) {
    return QString::number(QFileInfo(dirPath).lastModified().toMSecsSinceEpoch());
}

QString dirSizeBasisKey(const QString& dirPath) {
    const QFileInfoList kids = QDir(dirPath).entryInfoList(
        QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot);
    qint64 bytes = 0;
    for (const QFileInfo& fi : kids)
        if (fi.isFile()) bytes += fi.size();
    return dirSizeMtimeStamp(dirPath)
         + QLatin1Char('|') + QString::number(kids.size())
         + QLatin1Char('|') + QString::number(bytes);
}

struct DirSizeRow {
    bool    ok       = false;
    qint64  size     = 0;
    QString basis;
    qint64  computed = 0;
};

DirSizeRow dirSizeLookup(const QString& dirPath) {
    DirSizeRow r;
    QSqlDatabase db = th_impl::threadDb(
        AppSettings::instance().get("Cache/dbCacheMB", 64).toInt());
    if (!db.isOpen()) return r;
    QSqlQuery q(db);
    q.prepare("SELECT size, basis, computed FROM dirsize WHERE path = ?");
    q.addBindValue(dirPath);
    if (q.exec() && q.next()) {
        r.ok       = true;
        r.size     = q.value(0).toLongLong();
        r.basis    = q.value(1).toString();
        r.computed = q.value(2).toLongLong();
    }
    return r;
}

void dirSizeStore(const QString& dirPath, qint64 bytes, const QString& basis) {
    QSqlDatabase db = th_impl::threadDb(
        AppSettings::instance().get("Cache/dbCacheMB", 64).toInt());
    if (!db.isOpen()) return;
    QSqlQuery q(db);
    q.prepare("INSERT OR REPLACE INTO dirsize(path, size, basis, computed) "
              "VALUES(?, ?, ?, ?)");
    q.addBindValue(dirPath);
    q.addBindValue(bytes);
    q.addBindValue(basis);
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.exec();
}

} // namespace

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
#include "everything_engine.h"   // #251:Everything 瞬间统计(不可用时静默回退)
#include <QDirIterator>
#include <QElapsedTimer>
#include <QThreadPool>
#include <QThread>          // msleep:全树统计节流(2026-09-05)

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
    // #239(2026-09-04 用户令):file:///G:/1-media/2-shots 这类 URL 形态照常
    // 进入 —— 浏览器/网盘"复制链接"粘贴时常见。QUrl::toLocalFile 顺带解 %20
    // 等转义;file://G:/...(盘符被当 host 的残缺形态)解不出,剥前缀兜底。
    if (raw.startsWith("file:", Qt::CaseInsensitive)) {
        QString local = QUrl(raw).toLocalFile();
        if (local.isEmpty()) {
            const int p = raw.indexOf("://");
            if (p >= 0) local = raw.mid(p + 3);
        }
        if (!local.isEmpty()) raw = local.trimmed();
    }
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
            if (m_dirSizeRunning && m_dirSizeTarget == dp) {
                if (m_dirSizeStale) {
                    // #244 静默重校验:稳定显旧值,中途进度不惊动状态栏
                    ss = m_dirSizeStaleValue;
                    sizeText = formatSize(ss);
                } else {
                    ss = m_dirSizePartial;
                    sizeText = ss > 0 ? gazeTr("统计中… %1").arg(formatSize(ss))
                                      : gazeTr("统计中…");
                }
            } else if (m_dirSizeDone && m_dirSizeTarget == dp) {
                ss = m_dirSizeValue;
                sizeText = formatSize(ss);
            } else {
                // #244 缓存库路径:失效键未变且 <10 分钟 → 瞬间精确值;
                // 有旧值但可疑(键变/超龄) → 旧值先行+静默重算;
                // 无记录 → 走 #241 的「统计中」增长
                const DirSizeRow row = dirSizeLookup(dp);
                // 失效键快筛(2026-09-05):先只 stat 父目录 mtime 一段,mtime 与
                // 缓存一致就当作键未变直接命中;不一致才做全目录枚举的精确比对。
                // 全枚举是 O(子项数) 的同步磁盘活,原来每次选中都跑一遍。
                bool fresh = false;
                if (row.ok && QDateTime::currentMSecsSinceEpoch() - row.computed
                                  < kDirSizeRevalidateMs) {
                    if (row.basis.section(QLatin1Char('|'), 0, 0)
                        == dirSizeMtimeStamp(dp)) {
                        fresh = true;                       // mtime 未变:免全枚举
                    } else if (row.basis == dirSizeBasisKey(dp)) {
                        fresh = true;                       // mtime 变了但键其实没变
                    }
                }
                if (fresh) {
                    ss = row.size;
                    sizeText = formatSize(ss);
                } else if (tryEverythingDirStat(dp, /*forGrid*/ false)) {
                    // #251 Everything 瞬间统计已挂起(通常毫秒级返回)。
                    // 结果 applyEverythingDirSize 统一收尾:入库 + 刷新状态栏。
                    // 等结果期间体验与 #244 静默重校验一致:有旧值先显旧值、
                    // 没旧值显「统计中…」—— 由瞬间查询兜住,不再走递归长等。
                    if (row.ok) {
                        ss = row.size;
                        sizeText = formatSize(ss);
                    } else {
                        ss = 0;
                        sizeText = gazeTr("统计中…");
                    }
                } else {
                    // 引擎不可用/该目录已在途 → 原路 #241 递归统计,行为不劣于现状
                    startDirSizeRun(dp, row.ok ? row.size : -1);
                    if (row.ok) {
                        ss = row.size;
                        sizeText = formatSize(ss);   // 旧值先行,算完悄悄替换
                    } else {
                        ss = 0;
                        sizeText = gazeTr("统计中…");
                    }
                }
            }
        } else if (m_dirSizeRunning) {
            cancelDirSizeRun();   // 选中项不是(单个)目录:停掉旧统计
        }
    } else if (m_dirSizeRunning) {
        cancelDirSizeRun();
    }
    if (sc > 0) {
        auto paths = m_fileGrid->selectedPaths();
        // 多选混着"还没统计过的目录":尽力用 Everything 秒查把真值补上(入
        // m_dirSizes 后下次刷新即精确)。阈值 40:全选大目录时不至于一次发爆
        int unsizedDirs = 0;
        if (sc > 1 && !paths.isEmpty()) {
            for (const QString& p : paths) {
                if (!QFileInfo(p).isDir() || m_fileGrid->m_dirSizes.contains(p))
                    continue;
                if (++unsizedDirs <= 40) tryEverythingDirStat(p, /*forGrid*/ true);
            }
        }
        // 统计值只在方括号里出现一次;尾部「文件名+修改时间」不再重抄大小 ——
        // 否则同一趟状态栏会出现两个一模一样的大小(2026-09-09 用户报)
        QString shownSize = sizeText;
        if (shownSize.isEmpty()) {
            if (sc > 1 && unsizedDirs > 0 && ss == 0)
                shownSize = gazeTr("统计中…");   // 全没算出来:不亮 0,落地后自动刷新
            else
                shownSize = formatSize(ss);
        }
        text += gazeTr("  ·  已选 %1 项 · [%2]")
                    .arg(sc)
                    .arg(shownSize);
        if (!paths.isEmpty()) {
            QFileInfo fi(paths.first());
            text += "  " + fi.fileName()
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
// #244:staleSeed ≥ 0 = 缓存库有旧值但失效键可疑,本轮是静默重校验 —— 状态栏
// 稳定显旧值,线程侧不上报中途进度,只收尾;算完 applyDirSizeDone 悄悄换新值。
void MainWindow::startDirSizeRun(const QString& path, qint64 staleSeed) {
    if (m_dirSizeRunning) m_dirSizeStop->store(true);   // 取消旧一轮
    m_dirSizeStop = std::make_shared<std::atomic_bool>(false);
    auto stop = m_dirSizeStop;
    const quint64 runId = ++m_dirSizeRunId;
    const bool quiet = staleSeed >= 0;
    m_dirSizeRunning = true;
    m_dirSizeDone    = false;
    m_dirSizeTarget  = path;
    m_dirSizeStale   = quiet;
    m_dirSizeStaleValue = quiet ? qMax<qint64>(0, staleSeed) : 0;
    m_dirSizePartial = quiet ? m_dirSizeStaleValue : 0;
    QPointer<MainWindow> self(this);
    if (!m_dirSizePool) {
        m_dirSizePool = new QThreadPool(this);
        m_dirSizePool->setMaxThreadCount(1);   // 统计永远串行,不与全局池互抢
    }
    m_dirSizePool->start([self, path, stop, runId, quiet]() {
        qint64 sz = 0;
        QElapsedTimer t; t.start();
        qint64 lastPost = 0;
        bool stopped = false;
        // 节流(2026-09-05):每 512 项让出 1ms。统计是后台静默活,晚几秒无所谓;
        // 不节流时全树扫描把磁盘队列打满,缩略图/预览的读盘全被拖慢(用户报
        // "视频缩略图越来越慢+硬盘异响"的主要推手之一)。
        constexpr int kYieldEvery = 512;
        int sinceYield = 0;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (stop->load()) { stopped = true; break; }
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isFile()) sz += fi.size();
            if (++sinceYield >= kYieldEvery) {
                sinceYield = 0;
                QThread::msleep(1);
            }
            // 静默重校验不报中途进度:旧值还在状态栏稳稳挂着,免得数字跳回「统计中」
            if (!quiet && t.elapsed() - lastPost >= 24) {
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
    m_dirSizeStale = false;
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
    m_dirSizeStale   = false;
    m_dirSizeValue   = bytes;
    // #244:算完顺手入库(带当下失效键),下次选中同目录直接命中
    dirSizeStore(path, bytes, dirSizeBasisKey(path));
    updateStatus();
}

// ── #10 悬停文件夹大小(2026-09-05 用户令):文件页里悬停到文件夹,大小列
// 不再恒 0KB —— 库/会话缓存命中即时回;否则进共享单线程统计池串行算,
// 算完回填缓存 + dirsize 库 + 网格定点重绘。与选中统计共用节流纪律
// (每 512 项让出 1ms),悬停一次只带起一个目录,不会形成全盘扫描风暴。
void MainWindow::onGridDirSizeRequested(const QString& path) {
    if (m_gridDirSizes.contains(path)) {
        m_fileGrid->setDirSize(path, m_gridDirSizes.value(path));
        return;
    }
    if (m_gridDirPending.contains(path)) return;
    const DirSizeRow row = dirSizeLookup(path);
    bool fresh = false;
    if (row.ok && QDateTime::currentMSecsSinceEpoch() - row.computed
                                  < kDirSizeRevalidateMs) {
        // 与选中统计同一套失效键快筛:mtime 段一致就免全枚举
        if (row.basis.section(QLatin1Char('|'), 0, 0) == dirSizeMtimeStamp(path)
            || row.basis == dirSizeBasisKey(path))
            fresh = true;
    }
    if (fresh) {
        m_gridDirSizes.insert(path, row.size);
        if (m_gridDirSizes.size() > 256) m_gridDirSizes.clear();
        m_fileGrid->setDirSize(path, row.size);
        return;
    }
    // #251:先试 Everything 瞬间统计(不占单飞队列);命中即回,失败再走下面的
    // 内置递归排队 —— 结果由 applyEverythingDirSize 统一收尾
    if (tryEverythingDirStat(path, /*forGrid*/ true)) return;
    // 2026-09-06 用户报"文件夹卡在统计中":悬停请求全部排队进单线程池且
    // 不可取消,大目录一占池,后面的全部干等。改为单飞:同一时间只有一个
    // 悬停任务在跑,新请求只保留最新一个排队,并请运行中的旧任务尽早收手
    if (!m_gridDirPending.isEmpty()) {
        if (m_gridDirNext.isEmpty() || m_gridDirNext != path) {
            if (!m_gridDirNext.isEmpty()) m_gridDirPending.remove(m_gridDirNext);
            m_gridDirNext = path;
            m_gridDirPending.insert(path);
            if (m_gridSizeStop) m_gridSizeStop->store(true);
        }
        return;
    }
    startGridDirSize(path);
}

// 悬停统计任务(可中断):stop 置位即弃(不写缓存不落库),该目录由网格侧
// 清除"已问"标记,下次悬停重新发起;正常跑完则回填缓存/库/网格,并接力
// 排队中的最新请求
// 改名/删除前的统一出口(releaseGazeFileLocks 经元对象调到这里):
// 悬停统计递归扫描握着目录树句柄,会顶住同树内的改名操作
void MainWindow::abortGridDirSize() {
    if (m_gridSizeStop) m_gridSizeStop->store(true);
    if (!m_gridDirCurrent.isEmpty()) {
        if (m_fileGrid) m_fileGrid->retryDirSize(m_gridDirCurrent);
        m_gridDirCurrent.clear();
    }
    if (!m_gridDirNext.isEmpty()) {
        m_gridDirPending.remove(m_gridDirNext);
        m_gridDirNext.clear();
    }
}

void MainWindow::startGridDirSize(const QString& path) {
    m_gridDirCurrent = path;
    if (!m_gridSizeStop) m_gridSizeStop = std::make_shared<std::atomic_bool>(false);
    m_gridSizeStop->store(false);
    const auto stop = m_gridSizeStop;
    if (!m_dirSizePool) {
        m_dirSizePool = new QThreadPool(this);
        m_dirSizePool->setMaxThreadCount(1);
    }
    QPointer<MainWindow> self(this);
    m_dirSizePool->start([self, path, stop]() {
        qint64 sz = 0;
        constexpr int kYieldEvery = 512;
        int sinceYield = 0;
        bool stopped = false;
        QDirIterator it(path, QDir::Files | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (stop->load()) { stopped = true; break; }
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (fi.isFile()) sz += fi.size();
            if (++sinceYield >= kYieldEvery) {
                sinceYield = 0;
                QThread::msleep(1);   // 与选中统计同款节流:别把磁盘队列打满
            }
        }
        QMetaObject::invokeMethod(self, [self, path, sz, stopped]() {
            if (!self) return;
            self->m_gridDirPending.remove(path);
            self->m_gridDirCurrent.clear();
            if (stopped) {
                // 被更新的悬停请求打断:解除"已问"标记,用户再看它时重新发起
                if (self->m_fileGrid) self->m_fileGrid->retryDirSize(path);
                // 排队中的最新请求要接上,否则单飞管线空转,悬停大小永远"统计中"
                if (!self->m_gridDirNext.isEmpty()) {
                    const QString next = self->m_gridDirNext;
                    self->m_gridDirNext.clear();
                    self->startGridDirSize(next);
                }
                return;
            }
            self->m_gridDirSizes.insert(path, sz);
            if (self->m_gridDirSizes.size() > 256) self->m_gridDirSizes.clear();
            dirSizeStore(path, sz, dirSizeBasisKey(path));
            if (self->m_fileGrid) self->m_fileGrid->setDirSize(path, sz);
            if (!self->m_gridDirNext.isEmpty()) {
                const QString next = self->m_gridDirNext;
                self->m_gridDirNext.clear();
                self->startGridDirSize(next);
            }
        }, Qt::QueuedConnection);
    });
}

// ── #251 Everything 瞬间统计(状态栏选中 + 网格悬停共用) ──
// 引擎已部署 → 挂起异步查询(首次顺带拉起引擎实例)并登记在途集合去重;
// 引擎不可用/该目录已在途 → 返回 false,调用方照旧走内置递归统计。
// 命中结果写入 dirsize 库(键=目录 mtime),与 #244 失效键快筛无缝衔接:
// 本次之后同目录直接被缓存命中,连 es 都不必再查,直到 mtime 变动才重新走
// Everything 秒查 —— 统计速度与正确性两不误。
bool MainWindow::tryEverythingDirStat(const QString& path, bool forGrid) {
    QSet<QString>& pending = forGrid ? m_everythingGridStatPending
                                     : m_everythingDirStatPending;
    if (pending.contains(path)) return true;           // 已在途:别重复发
    if (!ev_impl::deployed()) return false;            // 没部署:一步都不发起
    pending.insert(path);
    const QString dp = path;
    const bool isGrid = forGrid;
    QPointer<MainWindow> self(this);
    ev_impl::ensureRunning(self, [self, dp, isGrid](bool ok) {
        if (!self) return;                             // 窗口没了,结果丢弃
        if (!ok) { self->applyEverythingDirSize(dp, false, 0, isGrid); return; }
        ev_impl::dirStatAsync(dp, self, [self, dp, isGrid](bool ok2, qint64 bytes) {
            if (self) self->applyEverythingDirSize(dp, ok2, bytes, isGrid);
        });
    });
    return true;
}

void MainWindow::applyEverythingDirSize(const QString& path, bool ok, qint64 bytes,
                                        bool forGrid) {
    if (forGrid) m_everythingGridStatPending.remove(path);
    else         m_everythingDirStatPending.remove(path);

    if (!ok) {
        // 引擎没起来/查询失败 → 回退各自的内置递归路径,体验不低于现状
        if (forGrid) {
            startGridDirSize(path);
        } else {
            const DirSizeRow row = dirSizeLookup(path);
            startDirSizeRun(path, row.ok ? row.size : -1);
        }
        return;
    }
    // 命中:以目录 mtime 作失效键入库(现有快筛比对 mtime 段,天然兼容)
    dirSizeStore(path, bytes, dirSizeMtimeStamp(path));
    if (forGrid) {
        m_gridDirSizes.insert(path, bytes);
        if (m_gridDirSizes.size() > 256) m_gridDirSizes.clear();
        if (m_fileGrid) m_fileGrid->setDirSize(path, bytes);
    } else {
        // 选中的还是这个目录 → 刷新状态栏(dirSizeLookup 现已命中,直接精确值)
        const auto paths = m_fileGrid->selectedPaths();
        if (paths.size() == 1 && QFileInfo(paths.first()).isDir()
            && paths.first().compare(path, Qt::CaseInsensitive) == 0)
            updateStatus();
    }
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
    if (!isFullScreen()) m_preFsState = windowState();   // 记住进前状态(最大化/普通),退出时还原
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

// F11 界面全屏的唯一退出出口(2026-09-08 用户报:ESC 退全屏丢了原布局)。
// 旧路径一律 showNormal(),把进前是最大化的窗口打回普通;G 全屏 2026-09-02
// 已用"进前记状态、退出还原"治过同样的病(m_preFullViewState),这里是
// 界面全屏的同一味药。G 全屏在场时让位给它的精确还原。
void MainWindow::exitFullscreen() {
    if (!isFullScreen()) return;
    if (m_fullView) { exitFullView(); return; }
    setWindowState(m_preFsState);
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
    // ── 占位符 token 双语兼容 ──
    // 模板是持久数据(存 ini),语言是会话状态:旧实现 token 走 gazeTr(跟语言),
    // 英文会话找不到中文 token、中文会话找不到英文 token,占位符原样漏进标题
    // (用户多次反馈"英文模式标题显示 Folder 不跟文件名")。修法:中英两套
    // token 都注册(英文对照与 translations batch JSON 一致),模板无论存哪国
    // 写法、运行在哪种语言,都能命中。长 token 恒先于短 token(花括号定界,
    // "{文件名}" 不会误伤 "{文件名 含扩展名}",但顺序仍保持先长后短以防万一)。
    auto subst2 = [&](const QString& zh, const QString& en, const QString& to) {
        subst(zh, to);   // 中文 token(字面,与语言无关)
        subst(en, to);   // 英文 token(字面)
    };
    subst2("路径", "Path",
           QDir::toNativeSeparators(hasSel ? fi.absoluteFilePath() : dir));
    subst2("文件夹", "Folder", QDir::toNativeSeparators(dir));
    subst2("文件夹名", "Folder name", dirName);
    // 目录没有"扩展名"这一说:两个名字令牌都给完整目录名,
    // 不能套 completeBaseName 的点切分(那会把 "A.B 文件夹" 截成 "A")
    subst2("文件名", "File name",
           isDirSel ? fi.fileName() : (hasFile ? fi.completeBaseName() : QString()));
    subst2("文件名 含扩展名", "File name with extension",
           hasSel ? fi.fileName() : QString());
    subst2("大小", "Size", sizeText);
    subst2("修改日期", "Date modified", mdate);
    subst2("创建日期", "Date created", cdate);
    subst2("颜色标签", "Color labels", label);
    subst2("宽", "Width", w > 0 ? QString::number(w) : QString());
    subst2("高", "Height", h > 0 ? QString::number(h) : QString());
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
