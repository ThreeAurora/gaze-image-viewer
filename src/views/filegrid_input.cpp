#include "filegrid.h"
#include "contextmenu.h"
#include "thumbnailer.h"
#include "livephoto.h"
#include "labelstore.h"
#include "settings.h"
#include "constants.h"
#include "shelldelete.h"
#include "iconlib.h"
#include "clipboardops.h"
#include "validname.h"
#include "exifdate.h"
#include "namesort.h"
#include "perflog.h"
#include "logger.h"
#include "i18n.h"

#include <set>
#include <algorithm>
#include <numeric>
#include <memory>
#include <array>

#include <QDrag>
#include <QMimeData>
#include <QApplication>
#include <QScrollBar>
#include <QResizeEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QToolTip>
#include <QFileInfo>
#include <QDir>
#include <QDesktopServices>
#include <QUrl>
#include <QFile>
#include <QCoreApplication>
#include <QThreadPool>
#include <QMessageBox>
#include <QInputDialog>
#include <QMenu>
#include <QLineEdit>
#include <QApplication>
#include <QProcess>
#include <QPainter>
#include <QPainterPath>
#include <QCollator>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QStyle>
#include <algorithm>
#include <cmath>
#include "filegrid_internal.h"

bool FileGrid::selectByPath(const QString& path) {
    // 目录还在后台装载(#6 异步化):先挂起,装载完成后 onDirScanDone 兑现。
    // 启动恢复/单实例转交在扫描完成前到达时不再空手而归
    if (m_loading && m_currentDir == QFileInfo(path).absolutePath()) {
        m_pendingSelectPath = path;
        return false;
    }
    // 外部传入的路径(argv 启动/地址栏文件跳转/上次文件恢复/旧存档)可能是
    // 旧版盘根连体形 "X://name" 或反斜杠形;内部条目自 joinEntryPath 收敛后
    // 一律是规范形 "X:/name"。先精确比(内部调用零开销),整体失配再按
    // cleanPath 归一比一次兜住存档
    const QString want = QDir::cleanPath(path);
    auto matches = [&](const FileEntry& e) {
        return e.path == path || QDir::cleanPath(e.path) == want;
    };
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (matches(m_entries[i])) {
            selectIndex(i);
            return true;
        }
    }
    // 不在当前筛选中:切回全部再试
    if (m_filterMode != FILTER_ALL) {
        setFilterMode(FILTER_ALL);
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (matches(m_entries[i])) { selectIndex(i); return true; }
        }
    }
    return false;
}

// 定位规则:首排贴顶;末排贴底;视口边缘半截露出的排→对齐贴边完整显示;
// 完全不可见→就近贴边(上方贴顶/下方贴底,最小滚动,不居中——用户令:
// 定位时"不能单纯地把它定位到中间",小文件夹居中尤其难看);完全可见→不动
void FileGrid::scrollToRow(int idx) {
    if (m_cols < 1 || m_entries.empty()) return;
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) return;
    ensureGeometry();
    if (idx >= static_cast<int>(m_geom.size())) return;
    int vpTop = verticalScrollBar()->value();
    int vpH = viewport()->height();

    // 瀑布流每卡片高度不同,不存在"行号×统一行高"的坐标模型:
    // 直接用 m_geom[idx] 顶/底边套用同一套"半截贴边/完全不可见就近贴边/完全可见不动"规则
    if (m_viewMode == VM_WATERFALL) {
        const QRect r = m_geom[idx];
        // 顶排贴顶:任何瀑布流首列首项 top==0 → 等价贴顶
        if (r.top() <= 0) { verticalScrollBar()->setValue(0); return; }
        // 底排贴底:卡片是全表最靠底的一批 → 拉到底,避免末尾留白
        int maxBottom = 0;
        for (const QRect& g : m_geom) maxBottom = std::max(maxBottom, g.bottom() + 1);
        if (r.bottom() + 1 >= maxBottom) {
            verticalScrollBar()->setValue(verticalScrollBar()->maximum());
            return;
        }
        bool topPartial    = (r.top() < vpTop) && (r.bottom() + 1 > vpTop);
        bool bottomPartial = (r.bottom() + 1 > vpTop + vpH) && (r.top() < vpTop + vpH);
        if (topPartial)
            verticalScrollBar()->setValue(r.top());
        else if (bottomPartial)
            verticalScrollBar()->setValue(r.bottom() + 1 - vpH);
        else if (r.bottom() + 1 <= vpTop)        // 完全在视口上方 → 贴顶
            verticalScrollBar()->setValue(std::max(0, r.top()));
        else if (r.top() >= vpTop + vpH)         // 完全在视口下方 → 贴底
            verticalScrollBar()->setValue(r.bottom() + 1 - vpH);
        return;
    }

    int row = idx / m_cols;
    int lastRow = (static_cast<int>(m_entries.size()) - 1) / m_cols;
    int rowH = cardH(idx) + m_spacing;
    int y = row * rowH;
    if (row == 0) {
        verticalScrollBar()->setValue(0);
        return;
    }
    if (row == lastRow) {
        verticalScrollBar()->setValue(verticalScrollBar()->maximum());
        return;
    }
    bool topPartial    = (y < vpTop) && (y + rowH > vpTop);              // 顶部半截露出
    bool bottomPartial = (y + rowH > vpTop + vpH) && (y < vpTop + vpH);  // 底部半截露出
    if (topPartial)
        verticalScrollBar()->setValue(y);                                // 贴顶完整展示
    else if (bottomPartial)
        verticalScrollBar()->setValue(y + rowH - vpH);                   // 贴底完整展示
    else if (y + rowH <= vpTop)
        verticalScrollBar()->setValue(y);                                // 完全在上方 → 贴顶
    else if (y >= vpTop + vpH)
        verticalScrollBar()->setValue(y + rowH - vpH);                   // 完全在下方 → 贴底
}

void FileGrid::selectIndex(int idx, bool scrollToVisible) {
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) return;
    m_selected.clear();
    m_selected.insert(idx);
    m_lastClicked = idx;

    // 滚动定位(联动选中时跳过:项已在视口内,二次滚动会形成无限反馈回路)
    if (scrollToVisible)
        scrollToRow(idx);

    refreshView();   // 只需重绘:几何未变
    emit selectionChanged(m_entries[idx].path);
}

// ═══════════════════════════════════════════
// 命中与画布事件
// ═══════════════════════════════════════════
int FileGrid::indexAt(const QPoint& canvasPos) {
    // 表头排序后 m_geomDirty=true,而重绘是异步排队:此刻若有鼠标点击命中,
    // 不补建几何就会按旧 m_geom/m_byY 命中到重排前的错条目
    ensureGeometry();
    const int from = lowerBoundRow(canvasPos.y() - m_maxCardH);
    for (int k = from; k < static_cast<int>(m_byY.size()); ++k) {
        const int i = m_byY[k];
        const QRect& r = m_geom[i];
        if (r.top() > canvasPos.y()) break;
        if (r.contains(canvasPos)) return i;
    }
    return -1;
}

QRect FileGrid::cardRect(int idx) const {
    return (idx >= 0 && idx < static_cast<int>(m_geom.size())) ? m_geom[idx]
                                                               : QRect();
}

void FileGrid::setHovered(int idx) {
    if (idx == m_hoverIdx) return;
    if (m_hoverIdx >= 0) m_canvas->update(cardRect(m_hoverIdx).adjusted(-4, -4, 4, 4));
    m_hoverIdx = idx;
    if (idx >= 0) {
        m_canvas->update(cardRect(idx).adjusted(-4, -4, 4, 4));
        // #10(2026-09-05 用户令):悬停到文件夹时它的"大小"不能再是 0KB ——
        // 向主窗要统计值(库/缓存命中即时回,否则后台算);同一目录只发一次。
        // 2026-09-06 复查:悬停即发起会和缩略图解码抢磁盘(扫过一排文件夹 =
        // 连环递归扫描,用户报浏览卡顿)——改为驻留 450ms 才发起
        if (m_entries[idx].isDir
            && !m_dirSizes.contains(m_entries[idx].path)
            && !m_dirSizeAsked.contains(m_entries[idx].path)) {
            m_dirSizeHoverPath = m_entries[idx].path;
            m_dirSizeTimer.start(450);
        } else if (!m_entries[idx].isDir) {
            m_dirSizeTimer.stop();
        }
    } else {
        m_dirSizeTimer.stop();
    }
}

// 大小列/详细行/悬浮提示共用的文案:目录=统计值(未知则"统计中…"),文件=常规
QString FileGrid::entrySizeText(const FileEntry& e) const {
    if (!e.isDir)
        return m_sizeBytes ? QString::number(e.size) + QLatin1String(" B")
                           : formatSize(e.size);
    const auto it = m_dirSizes.constFind(e.path);
    if (it != m_dirSizes.constEnd())
        return m_sizeBytes ? QString::number(*it) + QLatin1String(" B")
                           : formatSize(*it);
    return gazeTr("统计中…");
}

// 统计任务被更新请求中断:解除该目录的"已问"标记,下次悬停重新发起
void FileGrid::retryDirSize(const QString& dirPath) {
    m_dirSizeAsked.remove(dirPath);
}

// 主窗统计完成回填:定点重绘该行(统计中的字样换成了真值)
void FileGrid::setDirSize(const QString& dirPath, qint64 bytes) {
    m_dirSizes.insert(dirPath, bytes);
    m_dirSizeAsked.insert(dirPath);
    if (m_dirSizes.size() > 4096) m_dirSizes.clear();   // 会话封顶(#248 排序批量补值放大)
    // #248 大小排序:目录吃饱精确值 → 30ms 合并窗口重排,文件夹按真实体积归位
    if (m_sortCol == SORT_SIZE) { m_dirResortTimer.start(); return; }
    const int i = m_pathRow.value(dirPath, -1);
    if (i < 0) return;
    ensureGeometry();
    m_canvas->update(cardRect(i));
}

// 悬停提示按需生成(原实现每建一张卡就拼一次日期串,滚动时纯浪费)
QString FileGrid::tipFor(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) return {};
    const FileEntry& e = m_entries[idx];
    const QDateTime birth = e.ctime > 0
        ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.ctime)) : QDateTime();
    const QDateTime mod = e.mtime > 0
        ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.mtime)) : QDateTime();
    const QString dash = gazeTr("—");
    // 2026-09-03 夜修:文件名与"创建:"之间丢过换行,两者挤同一行
    return e.name + "\n"
        + gazeTr("创建: ")   // 创建:
        + (birth.isValid() ? birth.toString("yyyy/MM/dd - HH:mm:ss") : dash) + "\n"
        + gazeTr("修改: ")   // 修改:
        + (mod.isValid() ? mod.toString("yyyy/MM/dd - HH:mm:ss") : dash) + "\n"
        + entrySizeText(e);   // #10:目录大小走统计
}

// 单击/多选语义(与原 FileCard::clicked → onCardClicked 完全一致)
// ═══════════════════════════════════════════
// 拖放(#81)落点判定与追加选中
int FileGrid::hitTest(const QPoint& canvasPos) { return indexAt(canvasPos); }

// 祖先控件坐标 → 画布内容坐标 → 条目。拖放落点判定的正确入口:
// m_canvas 是 QScrollArea 的内容控件,其局部坐标即内容坐标(与滚动无关),
// mapFrom 自动跨越视口与滚动偏移。直接 mapFrom(滚动容器) 会在滚动后错位。
int FileGrid::hitTestFrom(const QPoint& ancestorPos, const QWidget* from) {
    return m_canvas ? indexAt(m_canvas->mapFrom(from, ancestorPos)) : -1;
}

QString FileGrid::pathAt(int idx) const {
    return (idx >= 0 && idx < static_cast<int>(m_entries.size())) ? m_entries[idx].path
                                                                  : QString();
}

// #225:G 全屏胶片条的数据源 —— 目录全部文件(目录行不进条;隐藏项跟
// FileList/showHidden)。与网格筛选无关:条要"所有文件都参与进来",
// 网格筛成"图片"时条里也得有视频/音频;点条里被筛掉的条目走 selectByPath
// 的"切回全部再选"回退(filegrid_input.cpp)。
QStringList FileGrid::allFilePaths() const {
    QStringList out;
    out.reserve(static_cast<int>(m_allEntries.size()));
    for (const auto& e : m_allEntries)
        if (!e.isDir && (m_showHidden || !e.hidden)) out << e.path;
    return out;
}

void FileGrid::selectPathAdditive(const QString& path) {
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (m_entries[i].path != path) continue;
        m_selected.insert(i);
        m_lastClicked = i;
        refreshView();
        emit selectionChanged(path);   // 状态栏选中统计要跟着刷新
        return;
    }
}

// 拖出(#81):把选中的文件拖到资源管理器/其它程序
//   复制语义(CopyAction)。起拖需要移动超过阈值,否则单击选中会被吞掉。
// ═══════════════════════════════════════════
void FileGrid::onCanvasPressStart(const QPoint& pos) {
    m_dragOrigin    = pos;
    m_dragOriginIdx = indexAt(pos);
    m_dragStarted   = false;
}

bool FileGrid::maybeStartDrag(const QPoint& pos) {
    if (m_dragStarted || m_dragOriginIdx < 0) return false;
    if ((pos - m_dragOrigin).manhattanLength() < QApplication::startDragDistance())
        return false;
    // 起拖前确保拖的是"已选中"的条目:拖未选中的项时先补选中它
    if (!m_selected.contains(m_dragOriginIdx)) {
        m_selected.clear();
        m_selected.insert(m_dragOriginIdx);
        m_lastClicked = m_dragOriginIdx;
        refreshView();
    }
    const QStringList paths = selectedPaths();
    if (paths.isEmpty()) return false;

    auto* mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString& p : paths)
        urls << QUrl::fromLocalFile(QDir::fromNativeSeparators(p));
    mime->setUrls(urls);
    // 2026-09-09:内部起拖的可靠标记。落点端靠它区分"自己拖的"与"资源管理器
    // 拖入",不再依赖 QDropEvent::source()(Qt 文档:可能返回 nullptr)。
    mime->setData(QStringLiteral("application/x-gaze-internal-drag"), QByteArrayLiteral("1"));

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    // 拖拽缩略图:单文件用它的缩略图,多文件用第一个。
    // 2026-09-09:缩略图缺席的类型(mp3/md/文本等)必须回退卡片同款类型图标 ——
    // QDrag 没设置 pixmap 时 Windows 走默认拖放观感,和视频拖动(有缩略图)的
    // 红禁止样式不一致;任何 pixmap 都走同一套 OLE 反馈,观感即统一。
    // 图标比照片缩略图"虚胖"(周围一圈透明),用 64 档小一号,不然拖起来一大团。
    QPixmap cursor = m_thumbCache.value(paths.first());
    const bool iconOnly = cursor.isNull();
    if (iconOnly && m_dragOriginIdx >= 0
        && m_dragOriginIdx < static_cast<int>(m_entries.size())) {
        cursor = iconPixmap(m_entries[m_dragOriginIdx], 64);
    }
    if (!cursor.isNull()) {
        const int side = iconOnly ? 64 : 96;
        drag->setPixmap(cursor.scaled(side, side, Qt::KeepAspectRatio,
                                      Qt::SmoothTransformation));
    }
    m_dragStarted = true;
    // 拖放=移动,Ctrl+拖放=复制(2026-08-31 用户明令):源端两种动作都声明,
    // 默认动作给 Move,由落点端按 Ctrl 修饰键最终决定
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::MoveAction);
    return true;
}

void FileGrid::onCanvasRelease(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;

    const bool ctrl  = QApplication::keyboardModifiers() & Qt::ControlModifier;
    const bool shift = QApplication::keyboardModifiers() & Qt::ShiftModifier;

    if (ctrl) {
        if (m_selected.contains(index)) m_selected.remove(index);
        else                            m_selected.insert(index);
    } else if (shift && m_lastClicked >= 0) {
        const int lo = std::min(m_lastClicked, index);
        const int hi = std::max(m_lastClicked, index);
        for (int i = lo; i <= hi; ++i) m_selected.insert(i);
    } else {
        m_selected.clear();
        m_selected.insert(index);
    }
    m_lastClicked = index;
    refreshView();

    // 单击选中不再自动贴边对齐(2026-09-06 用户令:详细列表下选中行不许跳
    // 动上下滚动条)。该自动贴边是早期"点首/末排完整展示那一排"的需求,
    // 缩略图类模式保留;详细/列表行高小、半截行多,点哪都跳,已按令摘除。
    // 键盘导航(navigateSelection)的滚动保留——方向键翻页依赖它。
    if (!ctrl && m_viewMode != VM_DETAILS && m_viewMode != VM_LIST)
        scrollToRow(index);

    // 注:这里过去会 emit dirSelected 让文件树镜像选中同一目录(2026-09-03 加的)。
    // 用户裁决「文件页中选中某文件夹时,文件树应当依然在原处;只有双击打开该
    // 文件夹时,才该去选中它」——故整条反向联动链路已移除。
    // 双击目录卡走 onCanvasDblClick → MainWindow::navigateTo,树在那里同步。

    emit selectionChanged(m_entries[index].path);
}

// ═══════════════════════════════════════════
// #268 框选拖动(rubber band):从空白处按住左键拖矩形,松开选中框内全部条目。
// 单点空白单击(= 框退化为点) → 取消选择 —— 与"点击空白清空选中"同义。
// Ctrl/Shift 修饰键按下时不走这里(mousePressEvent 已挡),框选恒为替换选择。
// ═══════════════════════════════════════════
void FileGrid::beginRubber(const QPoint& pos) {
    m_rubberActive = true;
    m_rubberStart  = pos;
    m_rubberCur    = pos;
}

void FileGrid::updateRubber(const QPoint& pos) {
    // 只重绘旧框与新框的并集:拖动中逐帧整版重绘大目录会白费 CPU
    const QRect old = QRect(m_rubberStart, m_rubberCur).normalized();
    m_rubberCur = pos;
    if (!m_canvas) return;
    const QRect now = QRect(m_rubberStart, m_rubberCur).normalized();
    m_canvas->update(old.united(now).adjusted(-4, -4, 4, 4));
}

void FileGrid::endRubber(const QPoint& pos) {
    m_rubberCur = pos;
    if (!m_rubberActive) return;
    m_rubberActive = false;
    if (m_canvas) m_canvas->update();   // 擦掉选框

    const QRect rub = QRect(m_rubberStart, m_rubberCur).normalized();
    // 空白单击:框退化成一个点,没有任何条目可中 → 取消选择
    if (rub.width() < 3 && rub.height() < 3) {
        if (!m_selected.isEmpty()) {
            m_selected.clear();
            m_lastClicked = -1;
            refreshView();
            emit selectionChanged({});
        }
        return;
    }
    // 收集框内条目:卡片矩形与选框相交即算中。按 m_byY(顶边排序)遍历,
    // 得到的"当前落点/键盘起点"取视觉上最靠上的那个,与用户直觉一致
    ensureGeometry();
    QSet<int> hit;
    int first = -1;
    for (int k = 0; k < static_cast<int>(m_byY.size()); ++k) {
        const int i = m_byY[k];
        if (i < 0 || i >= static_cast<int>(m_geom.size())) continue;
        if (!m_geom[i].intersects(rub)) continue;
        hit.insert(i);
        if (first < 0) first = i;
    }
    if (hit.isEmpty()) {   // 框远离所有卡片:同样视为取消选择
        if (!m_selected.isEmpty()) {
            m_selected.clear();
            m_lastClicked = -1;
            refreshView();
            emit selectionChanged({});
        }
        return;
    }
    m_selected    = hit;
    m_lastClicked = first;
    refreshView();
    if (first >= 0 && first < static_cast<int>(m_entries.size()))
        emit selectionChanged(m_entries[first].path);   // 状态栏/预览随框选切换
}

void FileGrid::onCanvasMiddle(int index) {
    onCanvasRelease(index);
    if (auto* mw = window())
        QMetaObject::invokeMethod(mw, "requestSwitchMode",
            Q_ARG(QString, QStringLiteral("SwitchMode/middleClick")));
}

void FileGrid::onCanvasDblClick(int index) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) return;
    const QString path = m_entries[index].path;
    QFileInfo fi(path);
    if (!fi.exists()) return;

    // 双击 = 选中它并打开。两击之间手抖超过 startDragDistance(触控板尤其
    // 容易)会起拖,第一击的 release 被拖拽吞掉 → onCanvasRelease 没跑,
    // 选中/m_currentFile 停在上一个文件;随后的 dblclick 照样走到
    // requestSwitchMode → toggleViewer,拿旧 m_currentFile 进查看器,
    // 体感就是"双击这张,打开的是别的文件"(2026-09-08 用户报)。
    // 不带修饰键的双击必是单选:这里先补齐 onCanvasRelease 的单选分支,
    // 把选中与 m_currentFile 钉到双击的这一项上,再按类型分流。
    m_selected.clear();
    m_selected.insert(index);
    m_lastClicked = index;
    refreshView();
    emit selectionChanged(path);

    if (fi.isDir()) {
        if (auto* mw = window()) QMetaObject::invokeMethod(mw, "navigateTo",
            Q_ARG(QString, fi.absoluteFilePath()));
        return;
    }

    // Live Photo:双击播放实况(回车才是进查看器)。
    // 提取必须走后台线程——内嵌视频整段读盘 + ffmpeg remux 最多 10s,
    // 同步跑在 GUI 线程会把整个窗口冻住(连点几张即"卡死")
    auto info = LivePhoto::detect(path);
    if (info) {
        if (!info->embedded) {
            Logger::event(QStringLiteral("dblClick: open companion '%1'").arg(info->videoPath));
            QDesktopServices::openUrl(QUrl::fromLocalFile(info->videoPath));
            return;
        }
        Logger::event(QStringLiteral("dblClick: async extract+open '%1'").arg(path));
        QThreadPool::globalInstance()->start([path, info]() {
            const QString vp = LivePhoto::extractEmbeddedVideo(path, *info);
            if (vp.isEmpty()) return;
            QMetaObject::invokeMethod(qApp, [vp]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(vp));
            }, Qt::QueuedConnection);
        });
        return;
    }

    // #102 双击按类型分流:图像/视频归 Gaze 的查看器,其余一律交给系统默认程序。
    // "切换模式"设置只管 Gaze 自己能显示的内容 —— 一个 .zip/.docx 没有"模式"可切,
    // 把双击吞掉(设置成"什么都不做"时)只会让用户以为没响应。
    const QString ext = "." + fi.suffix().toLower();
    // RAW(#140)也归查看器:预览面板有专门的 RAW 形态(占位+按需全解按钮)
    if (!IMAGE_EXTS.count(ext) && !VIDEO_EXTS.count(ext) && !RAW_EXTS.count(ext)) {
        Logger::event(QStringLiteral("dblClick: system open '%1'").arg(path));
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
        return;
    }

    if (auto* mw = window())
        QMetaObject::invokeMethod(mw, "requestSwitchMode",
            Q_ARG(QString, QStringLiteral("SwitchMode/doubleClick")));
}

void FileGrid::onCanvasMenu(int index, const QPoint& globalPos) {
    if (index < 0 || index >= static_cast<int>(m_entries.size())) {
        // 空白处右键(2026-09-01 用户令):先自动取消选中文件/夹,再弹作用于
        // "当前文件夹"的菜单 —— 新建文件夹/在资源管理器中显示/全选/属性
        if (!m_selected.empty()) {
            m_selected.clear();
            m_lastClicked = -1;
            refreshView();
            emit selectionChanged({});
        }
        QMenu menu(viewport());
        menu.addAction(IconLib::appIcon("cmd_newFolder"), gazeTr("新建文件夹"),
                       this, [this]() { newFolder(); });
        menu.addAction(IconLib::appIcon("cmd_open"), gazeTr("在资源管理器中显示"),
                       this, [this]() {
                           if (!m_currentDir.isEmpty())
                               QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentDir));
                       });
        menu.addAction(gazeTr("全选"), this, [this]() { selectAllEntries(); });
        menu.addAction(IconLib::appIcon("cmd_openProperties"), gazeTr("属性"),
                       this, [this]() {
                           if (!m_currentDir.isEmpty())
                               showShellProperties(m_currentDir);
                       });
        menu.exec(globalPos);
        return;
    }
    // 右键落在未选中条目上 = 先选中它再弹菜单(资源管理器同款:菜单作用于
    // 右键所指);已在选中集里(含多选之一)则保持原选,批量动作按整组生效
    if (!m_selected.contains(index)) {
        m_selected.clear();
        m_selected.insert(index);
        m_lastClicked = index;
        refreshView();
        scrollToRow(index);
        emit selectionChanged(m_entries[index].path);
    }
    FileContextMenu menu(this, index, viewport());
    menu.exec(globalPos);
}

// FileOps/renameDialog = 关:就地改名(不弹对话框)。
// 在画布上盖一个单行编辑器压住文件名区域,回车提交 / ESC 取消。
// 用画布坐标(与 m_geom 同一坐标系),因此滚动时它随卡片一起移动。
void FileGrid::beginInlineRename() {
    if (m_lastClicked < 0 || m_lastClicked >= static_cast<int>(m_entries.size())) return;
    const QString path = m_entries[m_lastClicked].path;
    QFileInfo fi(path);
    if (!fi.exists()) return;
    endInlineRename(false);

    scrollToRow(m_lastClicked);
    const QRect card = cardRect(m_lastClicked);
    if (card.isEmpty()) return;
    const fg_impl::CardBoxes bx = fg_impl::boxesFor(card, m_viewMode, m_labelGap);
    const QRect nameR = bx.name.isNull() ? card : bx.name;

    m_renameIdx  = m_lastClicked;
    m_renamePath = path;
    m_renameEdit = new QLineEdit(fi.fileName(), m_canvas);
    m_renameEdit->setObjectName("renameEdit");   // 样式在应用级 QSS(#89 收敛)
    m_renameEdit->setGeometry(nameR.adjusted(-2, -2, 2, 2));
    const int dot = fi.fileName().lastIndexOf(QLatin1Char('.'));
    if (dot > 0) m_renameEdit->setSelection(0, dot);
    else         m_renameEdit->selectAll();
    m_renameEdit->show();
    m_renameEdit->setFocus();
    m_renameEdit->installEventFilter(this);   // 失焦提交

    connect(m_renameEdit, &QLineEdit::returnPressed, m_renameEdit,
            [this]() { endInlineRename(true); });
    auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), m_renameEdit);
    esc->setContext(Qt::WidgetShortcut);
    connect(esc, &QShortcut::activated, m_renameEdit, [this]() { endInlineRename(false); });
}

void FileGrid::endInlineRename(bool commit) {
    if (!m_renameEdit) return;
    const QString newName = m_renameEdit->text().trimmed();
    const QString oldPath = m_renamePath;
    m_renameEdit->deleteLater();
    m_renameEdit = nullptr;
    m_renameIdx  = -1;
    m_renamePath.clear();
    setFocus();
    // 编辑框盖住的那张卡片立刻按旧名字重画:下面每一条不成立的分支都停在"没改成"
    if (m_canvas) m_canvas->update();

    if (!commit || newName.isEmpty()) return;
    QFileInfo fi(oldPath);
    if (newName == fi.fileName()) return;
    // 就地编辑框什么字符都收:不校验就拼路径,"a/b" 会把文件静默送出目录
    if (const QString why = invalidNameReason(newName); !why.isEmpty()) {
        QMessageBox::warning(this, gazeTr("重命名"), why);
        return;
    }
    const QString np = QDir(fi.absolutePath()).filePath(newName);
    if (QFileInfo::exists(np)) {
        QMessageBox::warning(this, gazeTr("重命名"),
                             gazeTr("目标名已存在:\n") + np);
        return;
    }
    // #214:改名目标若正被预览播放,句柄不放 rename 会失败
    releaseGazeFileLocks({oldPath});
    if (!renameWithRetry(oldPath, np)) {
        QMessageBox::warning(this, gazeTr("重命名失败"), np);
        return;
    }
    m_preferPath = np;
    loadDirectory(m_currentDir);
}

// ═══════════════════════════════════════════
// 键盘导航
// ═══════════════════════════════════════════
void FileGrid::navigateSelection(int delta) {
    // 只拦"没有内容"。m_cols 不拦:独立双击图片启动直进查看器时网格从未
    // 显示过,m_layoutReady 守卫(#216)让它一直是 0 —— 按老写法滚轮/方向键
    // 在独立查看器里整条导航链静默失灵。scrollToRow 自己有同款门禁,
    // 隐藏态下调用它无害(不滚就是了)
    if (m_entries.empty()) return;
    int idx = m_lastClicked + delta;
    // Viewer/loopFileList:首尾相接(默认关=到头就停,与改造前一致)
    const int n = static_cast<int>(m_entries.size());
    if (AppSettings::instance().get("Viewer/loopFileList", false).toBool()) {
        idx = ((idx % n) + n) % n;
    } else {
        idx = std::max(0, std::min(idx, n - 1));
        if (idx == m_lastClicked) return;
    }
    if (idx == m_lastClicked) return;

    m_selected.clear();
    m_selected.insert(idx);
    m_lastClicked = idx;

    // 滚动定位交给 scrollToRow:键盘导航与点击/联动选中用同一套权威几何,
    // 且瀑布流模式下按 m_geom 走(原"行号×统一行高"在瀑布流里会滚到错位置)
    scrollToRow(idx);

    refreshView();   // 只需重绘:几何未变
    emit selectionChanged(m_entries[idx].path);
}

// ═══════════════════════════════════════════
// 选择扩展(编辑菜单)
// ═══════════════════════════════════════════
void FileGrid::selectAllEntries() {
    m_selected.clear();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        m_selected.insert(i);
    refreshView();   // 只需重绘:几何未变
    const int idx = firstSelectedIndex();
    emit selectionChanged(idx >= 0 ? m_entries[idx].path : QString());
}

void FileGrid::selectInvert() {
    QSet<int> inverted;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        if (!m_selected.contains(i)) inverted.insert(i);
    m_selected = inverted;
    refreshView();   // 只需重绘:几何未变
    const int idx = firstSelectedIndex();
    emit selectionChanged(idx >= 0 ? m_entries[idx].path : QString());
}

void FileGrid::selectByKind(int kind) {
    auto isImg = [](const FileEntry& e) { return IMAGE_EXTS.count(e.ext) > 0; };
    auto isVid = [](const FileEntry& e) { return VIDEO_EXTS.count(e.ext) > 0; };
    auto isAud = [](const FileEntry& e) { return AUDIO_EXTS.count(e.ext) > 0; };

    m_selected.clear();
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        const auto& e = m_entries[i];
        bool ok = false;
        switch (kind) {
        case KindFiles:   ok = !e.isDir; break;
        case KindDirs:    ok = e.isDir; break;
        case KindImages:  ok = isImg(e); break;
        case KindVideos:  ok = isVid(e); break;
        case KindAudio:   ok = isAud(e); break;
        }
        if (ok) m_selected.insert(i);
    }
    refreshView();   // 只需重绘:几何未变
    const int idx = firstSelectedIndex();
    emit selectionChanged(idx >= 0 ? m_entries[idx].path : QString());
}

int FileGrid::firstSelectedIndex() const {
    return m_selected.isEmpty() ? -1 : *m_selected.constBegin();
}

// ═══════════════════════════════════════════
// 颜色标记:对当前选中项设置(0=取消)
// ═══════════════════════════════════════════
void FileGrid::applyColorLabelToSelection(int color) {
    if (m_selected.isEmpty()) return;
    for (int idx : m_selected) {
        if (idx < 0 || idx >= static_cast<int>(m_entries.size())) continue;
        const QString& p = m_entries[idx].path;
        m_entries[idx].colorLabel = color;
        m_colorLabels.insert(p, color);
        LabelStore::instance().setColor(p, color);
        // 同步 m_allEntries(筛选切换后不丢)
        for (auto& e : m_allEntries)
            if (e.path == p) { e.colorLabel = color; break; }
    }
    // 绘制时直接读 m_entries[idx].colorLabel,重绘即可
    refreshView();
}

// ═══════════════════════════════════════════
// 键盘事件
// ═══════════════════════════════════════════
void FileGrid::keyPressEvent(QKeyEvent* event) {
    int key = event->key();
    AppSettings& st = AppSettings::instance();
    // 方向键语义可在设置→键盘配置:0=上一个/下一个文件(默认),1=滚动
    const int lr = st.get("Keyboard/leftRight", 0).toInt();
    const int ud = st.get("Keyboard/upDown", 0).toInt();
    auto pageScroll = [this](QScrollBar* bar, int dir) {
        bar->setValue(bar->value() + dir * bar->pageStep());
    };
    // 单字母键(C/V/X/S)只在无 Ctrl/Alt/Meta 时生效:Ctrl+C/V/X 是标准
    // 剪贴板语义,Ctrl+S 也不该被这些单字母动作吞掉
    // 键位归属:F/D = 颜色标签(应用级过滤器,见 MainWindow::eventFilter)
    const Qt::KeyboardModifiers mods = event->modifiers();
    const bool bare = (mods & (Qt::ControlModifier | Qt::AltModifier
                               | Qt::MetaModifier)) == 0;

    if (mods & Qt::ControlModifier) {
        // Ctrl+A 全选由主窗口"全选"菜单的快捷键承担,不在此重复
        if (key == Qt::Key_C || key == Qt::Key_X) {
            clipboardSetFiles(selectedPaths(), key == Qt::Key_X);
            event->accept();
        } else if (key == Qt::Key_V) {
            QStringList errs;
            const bool pasted = clipboardPasteInto(QDir(m_currentDir), &errs);
            if (pasted) {
                if (!errs.isEmpty())
                    QMessageBox::warning(this, gazeTr("部分项目未能粘贴"),
                                         errs.join(QLatin1Char('\n')));
                loadDirectory(m_currentDir);
            } else if (!errs.isEmpty()) {
                QMessageBox::warning(this, gazeTr("粘贴失败"),
                                     errs.join(QLatin1Char('\n')));
            }
            event->accept();
        } else {
            QScrollArea::keyPressEvent(event);
        }
    } else if (bare && (key == Qt::Key_C || key == Qt::Key_Left)) {
        if (lr == 1) pageScroll(horizontalScrollBar(), -1); else navigateSelection(-1);
    } else if (bare && (key == Qt::Key_V || key == Qt::Key_Right)) {
        if (lr == 1) pageScroll(horizontalScrollBar(), 1); else navigateSelection(1);
    } else if (key == Qt::Key_Up) {
        if (ud == 1) pageScroll(verticalScrollBar(), -1); else navigateSelection(-1);
    } else if (key == Qt::Key_Down) {
        if (ud == 1) pageScroll(verticalScrollBar(), 1); else navigateSelection(1);
    } else if (key == Qt::Key_Delete) {
        // Delete 键此前全应用无绑定:网格删除只认 S。排队执行同上(嵌套事件循环)
        QTimer::singleShot(0, this, [this]() { deleteSelection(); });
    } else if (bare && key == Qt::Key_S) {
        // 排队执行:确认框是嵌套事件循环,不要在按键派发栈里跑它。
        // 注:这不是那次闪退的根因(根因在 main.cpp 的对话框过滤器,
        // 它在 QMessageBox 构造期就调了 button()),此处只作防御。
        QTimer::singleShot(0, this, [this]() { deleteSelection(); });
    } else if (bare && key == Qt::Key_X) {
        QTimer::singleShot(0, this, [this]() { newFolder(); });
    } else {
        QScrollArea::keyPressEvent(event);
    }
}

// ═══════════════════════════════════════════
// 事件过滤器（Ctrl+滚轮缩放）
// ═══════════════════════════════════════════
bool FileGrid::eventFilter(QObject* obj, QEvent* event) {
    // 视口自身 Resize(垂直滚动条出现/消失改视口宽,主控件 resizeEvent 不触发):
    // 走同一个 30ms 合并定时器 → updateLayout 重排 + 详细态重推表头列几何
    if (obj == viewport() && event->type() == QEvent::Resize) {
        m_resizeTimer.start(30);
    }
    // 就地改名编辑器失焦 = 提交(点别处、切目录、按 Tab 都算)
    if (m_renameEdit && obj == m_renameEdit && event->type() == QEvent::FocusOut) {
        endInlineRename(true);
        return true;
    }
    // 搜索条输入框:Enter/↓ 下一个,Shift+Enter/↑ 上一个,Esc 收条还给列表
    if (m_findEdit && obj == m_findEdit && event->type() == QEvent::KeyPress) {
        auto* ke = static_cast<QKeyEvent*>(event);
        switch (ke->key()) {
        case Qt::Key_Escape:             closeFind();        return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:              findStep(ke->modifiers() & Qt::ShiftModifier ? -1 : 1); return true;
        case Qt::Key_Down:               findStep(1);        return true;
        case Qt::Key_Up:                 findStep(-1);       return true;
        default: break;
        }
    }
    if (event->type() == QEvent::Wheel) {
        auto* we = static_cast<QWheelEvent*>(event);
        if (we->modifiers() & Qt::ControlModifier) {
            setCardSize(m_cardSize + (we->angleDelta().y() > 0 ? 10 : -10));
            return true;
        }
    }
    return QScrollArea::eventFilter(obj, event);
}

// 焦点线在 paintCard 里按 hasFocus() 画;没有这两个钩子,失焦后那条线会留在原地
// 换成假指示器。底色/框色兼随焦点变化(2026-09-02:网格有焦点=亮蓝,失焦=暗蓝),
// 所以无论单选多选都必须整版重绘 —— 不能像旧版只在多选时刷新。
void FileGrid::focusInEvent(QFocusEvent* event) {
    QScrollArea::focusInEvent(event);
    refreshView();
}

void FileGrid::focusOutEvent(QFocusEvent* event) {
    QScrollArea::focusOutEvent(event);
    refreshView();
}

