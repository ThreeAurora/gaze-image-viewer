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
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (m_entries[i].path == path) {
            selectIndex(i);
            return true;
        }
    }
    // 不在当前筛选中:切回全部再试
    if (m_filterMode != FILTER_ALL) {
        setFilterMode(FILTER_ALL);
        for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
            if (m_entries[i].path == path) { selectIndex(i); return true; }
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
    if (idx >= 0) m_canvas->update(cardRect(idx).adjusted(-4, -4, 4, 4));
}

// 悬停提示按需生成(原实现每建一张卡就拼一次日期串,滚动时纯浪费)
QString FileGrid::tipFor(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(m_entries.size())) return {};
    const FileEntry& e = m_entries[idx];
    const QDateTime birth = e.ctime > 0
        ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.ctime)) : QDateTime();
    const QDateTime mod = e.mtime > 0
        ? QDateTime::fromSecsSinceEpoch(static_cast<qint64>(e.mtime)) : QDateTime();
    const QString dash = QString::fromUtf8("\xe2\x80\x94");
    // 2026-09-03 夜修:文件名与"创建:"之间丢过换行,两者挤同一行
    return e.name + "\n"
        + QString::fromUtf8("\xe5\x88\x9b\xe5\xbb\xba: ")   // 创建:
        + (birth.isValid() ? birth.toString("yyyy/MM/dd - HH:mm:ss") : dash) + "\n"
        + QString::fromUtf8("\xe4\xbf\xae\xe6\x94\xb9: ")   // 修改:
        + (mod.isValid() ? mod.toString("yyyy/MM/dd - HH:mm:ss") : dash) + "\n"
        + (m_sizeBytes ? QString::number(e.size) + " B" : formatSize(e.size));
}

// 单击/多选语义(与原 FileCard::clicked → onCardClicked 完全一致)
// ═══════════════════════════════════════════
// 拖放(#81)落点判定与追加选中
int FileGrid::hitTest(const QPoint& canvasPos) { return indexAt(canvasPos); }

QString FileGrid::pathAt(int idx) const {
    return (idx >= 0 && idx < static_cast<int>(m_entries.size())) ? m_entries[idx].path
                                                                  : QString();
}

void FileGrid::selectPathAdditive(const QString& path) {
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i) {
        if (m_entries[i].path != path) continue;
        m_selected.insert(i);
        m_lastClicked = i;
        refreshView();
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

    auto* drag = new QDrag(this);
    drag->setMimeData(mime);
    // 拖拽缩略图:单文件用它的缩略图,多文件用第一个
    QPixmap cursor = m_thumbCache.value(paths.first());
    if (!cursor.isNull()) {
        drag->setPixmap(cursor.scaled(96, 96, Qt::KeepAspectRatio,
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

    // 普通单击选中首排/末排文件时,自动贴边完整展示那一排
    if (!ctrl) scrollToRow(index);

    // 2026-09-03:鼠标单选**目录**卡 → 通知主窗口,让文件树镜像选中同一条目
    // (只在这里发:loadDirectory 自动选中首项/方向键盘选都不触发,避免树瞎跳)
    if (QFileInfo(m_entries[index].path).isDir())
        emit dirSelected(m_entries[index].path);

    emit selectionChanged(m_entries[index].path);
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
        menu.addAction(IconLib::appIcon("cmd_newFolder"), QString::fromUtf8("新建文件夹"),
                       this, [this]() { newFolder(); });
        menu.addAction(IconLib::appIcon("cmd_open"), QString::fromUtf8("在资源管理器中显示"),
                       this, [this]() {
                           if (!m_currentDir.isEmpty())
                               QDesktopServices::openUrl(QUrl::fromLocalFile(m_currentDir));
                       });
        menu.addAction(QString::fromUtf8("全选"), this, [this]() { selectAllEntries(); });
        menu.addAction(IconLib::appIcon("cmd_openProperties"), QString::fromUtf8("属性"),
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
    m_renameEdit->setStyleSheet(QString::fromUtf8(
        "QLineEdit{background:%1;color:%2;border:1px solid %3;"
        "font-size:12px;padding:0 2px;}").arg(C_CONTENT, C_TEXT, C_ACCENT));
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
        QMessageBox::warning(this, QString::fromUtf8("重命名"), why);
        return;
    }
    const QString np = QDir(fi.absolutePath()).filePath(newName);
    if (QFileInfo::exists(np)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
                             QString::fromUtf8("目标名已存在:\n") + np);
        return;
    }
    if (!QFile::rename(oldPath, np)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名失败"), np);
        return;
    }
    m_preferPath = np;
    loadDirectory(m_currentDir);
}

// ═══════════════════════════════════════════
// 键盘导航
// ═══════════════════════════════════════════
void FileGrid::navigateSelection(int delta) {
    if (m_entries.empty() || m_cols < 1) return;
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
}

void FileGrid::selectInvert() {
    QSet<int> inverted;
    for (int i = 0; i < static_cast<int>(m_entries.size()); ++i)
        if (!m_selected.contains(i)) inverted.insert(i);
    m_selected = inverted;
    refreshView();   // 只需重绘:几何未变
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
                    QMessageBox::warning(this, QString::fromUtf8("部分项目未能粘贴"),
                                         errs.join(QLatin1Char('\n')));
                loadDirectory(m_currentDir);
            } else if (!errs.isEmpty()) {
                QMessageBox::warning(this, QString::fromUtf8("粘贴失败"),
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

