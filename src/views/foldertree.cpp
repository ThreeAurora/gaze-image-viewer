#include "foldertree.h"
#include "constants.h"
#include "fileentry.h"
#include "settings.h"
#include "settings.h"
#include "settings.h"
#include "settings.h"

#include <windows.h>
#include <shellapi.h>

#include <QHeaderView>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QFontMetrics>
#include <QApplication>
#include <QImage>

// ═══════════════════════════════════════════
// ArrowStyle
// ═══════════════════════════════════════════
void ArrowStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option,
                               QPainter* painter, const QWidget* widget) const {
    if (element == PE_IndicatorBranch) {
        // 有子文件夹才绘制展开箭头；叶子节点什么都不画，
        // 避免默认 style 给无子文件夹的目录也留下“展开按钮/分支装饰”
        if (option->state & State_Children) {
            bool expanded = option->state & State_Open;
            QRect r = option->rect;
            int cx = r.center().x();
            int cy = r.center().y();
            int sz = 4;

            painter->save();
            painter->setRenderHint(QPainter::Antialiasing, true);
            painter->setPen(Qt::NoPen);
            painter->setBrush(QColor("#FFFFFF"));   // 白色箭头常显,可展开处一目了然
            QPolygonF tri;
            if (expanded) {
                tri << QPointF(cx - sz, cy - sz / 2.0)
                    << QPointF(cx + sz, cy - sz / 2.0)
                    << QPointF(cx, cy + sz);
            } else {
                tri << QPointF(cx - sz / 2.0, cy - sz)
                    << QPointF(cx - sz / 2.0, cy + sz)
                    << QPointF(cx + sz, cy);
            }
            painter->drawPolygon(tri);
            painter->restore();
        }
        return;
    }
    QProxyStyle::drawPrimitive(element, option, painter, widget);
}

// ═══════════════════════════════════════════
// FolderTree
// ═══════════════════════════════════════════

// 统计子文件夹（含隐藏目录），用于判断是否应显示展开箭头
static bool hasVisibleSubdirs(const QString& path) {
    QDir dir(path);
    const QStringList list = dir.entryList(
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot);
    return !list.isEmpty();
}

FolderTree::FolderTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setIndentation(16);
    setAnimated(true);
    // 去掉 item 上的虚线焦点框，避免“桌面”这类当前项出现与其他磁盘不一致的描边
    setFocusPolicy(Qt::NoFocus);
    // 去掉 item 上的虚线焦点框，避免“桌面”这类当前项出现与其他磁盘不一致的描边
    setFocusPolicy(Qt::NoFocus);
    // 启用自定义展开箭头：有子文件夹才画三角，叶子目录彻底不画分支装饰
    // QWidget::setStyle 不接管所有权,显式 setParent(this) 让树销毁时一并释放
    auto* arrowStyle = new ArrowStyle;
    arrowStyle->setParent(this);
    setStyle(arrowStyle);
    setStyleSheet(QString(
        "QTreeWidget{background:%1;color:%2;border:none;font-size:11px;outline:0;}"
        "QTreeWidget::item{padding:2px 0;outline:0;}"
        "QTreeWidget::item:hover{background:%3;}"
        "QTreeWidget::item:selected{background:%4;color:#FFF;}"
    ).arg(C_SIDEBAR, C_TREE_TEXT, C_TREE_HOVER, C_TREE_SELECT));

    setIconSize(QSize(16, 16));

    makeIcons();

    connect(this, &QTreeWidget::itemClicked, this, &FolderTree::onItemClicked);
    // 右键:CustomContextMenu 把坐标交给我们自建菜单(默认策略只会弹 Qt 的空菜单)
    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested,
            this, &FolderTree::showContextMenu);
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem* item) {
        // 懒加载：展开时加载子项
        if (item->childCount() == 1 && item->child(0)->text(0).isEmpty()) {
            delete item->takeChild(0);
            loadChildren(item);
        }
    });

    // Browser/showDesktopInTree:仅此一项会增减根行,变化时重建(其余设置与树无关)
    m_showDesktop = AppSettings::instance().get("Browser/showDesktopInTree", true).toBool();
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        const bool now = AppSettings::instance()
                             .get("Browser/showDesktopInTree", true).toBool();
        if (now == m_showDesktop) return;
        m_showDesktop = now;
        clear();
        loadDrives();
    });
}

void FolderTree::makeIcons() {
    // 使用 QImage 绘制（跨平台兼容性更好）
    // 文件夹图标
    {
        QImage img(18, 18, QImage::Format_ARGB32);
        img.fill(Qt::transparent);
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor("#E8B830"));
        p.drawRoundedRect(QRectF(1, 4, 16, 12), 2, 2);
        p.drawRoundedRect(QRectF(1, 1, 8, 5), 2, 2);
        p.end();
        m_folderIcon = QIcon(QPixmap::fromImage(img));
    }

    // 隐藏文件夹图标：同样的文件夹图形，半透明弱化
    {
        QPixmap pm = m_folderIcon.pixmap(18, 18);
        QPixmap dim(pm.size());
        dim.fill(Qt::transparent);
        QPainter p(&dim);
        p.setOpacity(0.45);
        p.drawPixmap(0, 0, pm);
        p.end();
        m_folderIconDim = QIcon(dim);
    }

    // 桌面图标:Windows 系统桌面图标(SIID_DESKTOPPC)
    {
        SHSTOCKICONINFO si = {};
        si.cbSize = sizeof(si);
        if (SUCCEEDED(SHGetStockIconInfo(SIID_DESKTOPPC,
                SHGSI_ICON | SHGSI_LARGEICON, &si)) && si.hIcon) {
            QImage img = hiconToQImage(si.hIcon);
            if (!img.isNull()) m_desktopIcon = QIcon(QPixmap::fromImage(img));
            DestroyIcon(si.hIcon);
        }
        if (m_desktopIcon.isNull()) {   // 回退:自绘显示器
            QImage img(18, 18, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setRenderHint(QPainter::Antialiasing);
            p.setPen(QPen(QColor("#0078D7"), 1.5));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(QRectF(2, 2, 14, 10), 2, 2);
            p.drawLine(QPoint(6, 12), QPoint(12, 12));
            p.drawLine(QPoint(9, 12), QPoint(9, 16));
            p.drawLine(QPoint(5, 16), QPoint(13, 16));
            p.end();
            m_desktopIcon = QIcon(QPixmap::fromImage(img));
        }
    }

    // 硬盘图标:用 Windows 真实磁盘图标(SHGetFileInfo on C:\)
    {
        SHFILEINFOW sfi = {};
        if (SHGetFileInfoW(L"C:\\", 0, &sfi, sizeof(sfi), SHGFI_ICON | SHGFI_LARGEICON)
            && sfi.hIcon) {
            QImage img = hiconToQImage(sfi.hIcon);
            if (!img.isNull()) m_driveIcon = QIcon(QPixmap::fromImage(img));
            DestroyIcon(sfi.hIcon);
        } else {
            QImage img(18, 18, QImage::Format_ARGB32);
            img.fill(Qt::transparent);
            QPainter p(&img);
            p.setPen(QPen(QColor("#888"), 1));
            p.setBrush(Qt::NoBrush);
            p.drawRect(QRectF(2, 2, 12, 13));
            p.drawRect(QRectF(11, 4, 3, 3));
            p.end();
            m_driveIcon = QIcon(QPixmap::fromImage(img));
        }
    }
}

void FolderTree::loadDrives() {
    // 桌面（与磁盘一致：懒加载；只有存在可见子文件夹时才留展开箭头）
    // Browser/showDesktopInTree 关掉则整行不出现,盘符成为首行
    if (m_showDesktop) {
        auto* desktopItem = new QTreeWidgetItem(this);
        QString desktopPath = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
        desktopItem->setText(0, "\xe6\xa1\x8c\xe9\x9d\xa2"); // 桌面
        desktopItem->setIcon(0, m_desktopIcon);
        desktopItem->setData(0, Qt::UserRole, desktopPath);
        if (hasVisibleSubdirs(desktopPath))
            desktopItem->addChild(new QTreeWidgetItem); // 占位（懒加载标记）
        desktopItem->setExpanded(false);   // 桌面默认折叠
    }

    // 枚举 A-Z 盘符(显示 Windows 卷标:如 "Cell (C:)",无卷标则只显示盘符)
    for (char drive = 'A'; drive <= 'Z'; ++drive) {
        QString root = QString("%1:/").arg(drive);
        QFileInfo fi(root);
        if (fi.exists()) {
            QStorageInfo si(root);
            QString label = si.name();
            QString text = label.isEmpty()
                ? QString("%1: (%2:)").arg(drive).arg(drive)
                : QString("%1 (%2:)").arg(label).arg(drive);
            auto* item = new QTreeWidgetItem(this);
            item->setText(0, text);
            item->setIcon(0, m_driveIcon);
            item->setData(0, Qt::UserRole, root);
            if (hasVisibleSubdirs(root))
                item->addChild(new QTreeWidgetItem); // 占位（懒加载标记）
        }
    }
}

void FolderTree::loadChildren(QTreeWidgetItem* item) {
    QString path = item->data(0, Qt::UserRole).toString();
    QDir dir(path);
    QFileInfoList list = dir.entryInfoList(
        QDir::Dirs | QDir::Hidden | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo& fi : list) {
        auto* child = new QTreeWidgetItem;
        child->setText(0, fi.fileName());
        // 隐藏文件夹用淡灰文字 + 半透明图标；普通文件夹正常
        bool hidden = fi.isHidden() || fi.fileName().startsWith('.');
        child->setIcon(0, hidden ? m_folderIconDim : m_folderIcon);
        child->setData(0, Qt::UserRole, fi.absoluteFilePath());
        child->setForeground(0, QBrush(QColor(hidden ? C_TEXT_HIDDEN : C_TREE_TEXT)));
        // 检测是否有子文件夹（含隐藏）；没有就不留展开按钮
        if (hasVisibleSubdirs(fi.absoluteFilePath()))
            child->addChild(new QTreeWidgetItem); // 占位
        item->addChild(child);
    }
}

void FolderTree::mouseDoubleClickEvent(QMouseEvent* event) {
    // Qt 默认在快速第二次点击分支箭头时会吞掉该次双击（视为双击事件但不切换展开），
    // 导致连续快速点击“需要间隔约 300ms”。这里在双击时也直接切换展开/收起。
    if (QTreeWidgetItem* item = itemAt(event->pos())) {
        if (item->childCount() > 0) {
            item->setExpanded(!item->isExpanded());
            event->accept();
            return;
        }
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void FolderTree::focusPath(const QString& dirPath) {
    QFileInfo fi(dirPath);
    if (!fi.exists()) return;

    // 找到最近的匹配项并展开
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto* item = topLevelItem(i);
        QString itemPath = item->data(0, Qt::UserRole).toString();
        if (fi.absoluteFilePath().startsWith(itemPath)) {
            setCurrentItem(item);
            item->setExpanded(true);
            break;
        }
    }
}

void FolderTree::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    emit folderSelected(item->data(0, Qt::UserRole).toString());
}

// ═══════════════════════════════════════════
// 右键菜单 —— 操作对象是"文件夹"本身(网格那份操作的是文件)
//   新建文件夹 / 剪切 / 复制 / 粘贴 / 删除 / 重命名 / 复制到.. / 移动到...
//   显示子文件夹中的文件 / 搜索... / 用资源管理器打开文件 / 属性
// ═══════════════════════════════════════════

QString FolderTree::pathOf(const QTreeWidgetItem* item) {
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool FolderTree::isVolumeRoot(const QString& path) {
    return !path.isEmpty() && QDir(path).isRoot();
}

// 树里只物化了"展开过的那些行",所以这里查的是已存在的节点;查不到通常意味着
// 该目录在某条折叠分支下面 —— 那也正是不需要维护的情况(展开时会重扫)
QTreeWidgetItem* FolderTree::itemForPath(const QString& path) {
    if (path.isEmpty()) return nullptr;
    const QString want = QDir::cleanPath(path);
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        const QString p = pathOf(*it);
        if (p.isEmpty()) continue;           // 懒加载占位行不算命中
        if (QDir::cleanPath(p).compare(want, Qt::CaseInsensitive) == 0) return *it;
    }
    return nullptr;
}

QStringList FolderTree::selectedPaths() const {
    QStringList out;
    const QList<QTreeWidgetItem*> items = selectedItems();
    for (const QTreeWidgetItem* it : items) {
        const QString p = pathOf(it);
        if (!p.isEmpty()) out << p;
    }
    return out;
}

// 某一层结构变了(新建/粘贴/移入/移出/改名),让树上的这一层重新对齐磁盘。
// 未物化或折叠着的分支一律退回"占位":展开时必然重扫,既正确又不白扫。
void FolderTree::refreshNode(const QString& dirPath) {
    QTreeWidgetItem* it = itemForPath(dirPath);
    if (!it) return;
    const bool placeholder =
        (it->childCount() == 1 && it->child(0)->text(0).isEmpty());
    if (placeholder || !it->isExpanded()) {
        while (it->childCount()) delete it->takeChild(0);
        if (hasVisibleSubdirs(dirPath)) it->addChild(new QTreeWidgetItem);
        return;
    }
    while (it->childCount()) delete it->takeChild(0);
    loadChildren(it);
}

void FolderTree::removeNodes(const QStringList& paths) {
    for (const QString& p : paths) {
        QTreeWidgetItem* it = itemForPath(p);
        if (!it) continue;
        if (QTreeWidgetItem* par = it->parent()) par->removeChild(it);
        else takeTopLevelItem(indexOfTopLevelItem(it));
        delete it;   // removeChild 只是摘出树,所有权仍在调用方
    }
}

void FolderTree::reportErrors(const QStringList& errors, const QString& title) {
    if (errors.isEmpty()) return;
    QString text = errors.join(QLatin1Char('\n'));
    if (errors.size() > 8)
        text = errors.mid(0, 8).join(QLatin1Char('\n'))
             + QString::fromUtf8("\n…另有 %1 条").arg(errors.size() - 8);
    QMessageBox::warning(this, title, text);
}

// Windows 目录名禁区。不接受分隔符是硬要求:旧实现把用户输入直接拼进路径,
// 一个 "a/b" 就能让"重命名"把整个文件夹搬到别处去(表面上什么都没发生)。
static bool isLegalFolderName(const QString& name) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name)
        if (bad.contains(c) || c.unicode() < 0x20) return false;
    return true;
}

void FolderTree::newFolderInto(QTreeWidgetItem* base) {
    const QString dir = pathOf(base);
    if (dir.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("新建文件夹"), QString::fromUtf8("名称:"),
        QLineEdit::Normal, QString::fromUtf8("新建文件夹"), &ok).trimmed();
    if (!ok) return;
    if (!isLegalFolderName(name)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("名称不能包含 / \\ : * ? \" < > | 也不能为空"));
        return;
    }
    const QString full = QDir(dir).filePath(name);
    if (QFileInfo::exists(full)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("同名文件夹已存在:\n") + full);
        return;
    }
    if (!QDir().mkdir(full)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("创建失败:\n") + full);
        return;
    }
    refreshNode(dir);
    if (QTreeWidgetItem* it = itemForPath(dir)) it->setExpanded(true);
    emit foldersChanged({dir}, {});
}

void FolderTree::pasteInto(QTreeWidgetItem* base) {
    const QString dir = pathOf(base);
    if (dir.isEmpty()) return;
    QStringList errs;
    const bool ok = clipboardPasteInto(QDir(dir), &errs);
    reportErrors(errs, ok ? QString::fromUtf8("部分项目未能粘贴")
                          : QString::fromUtf8("粘贴失败"));
    if (!ok) return;
    refreshNode(dir);
    if (QTreeWidgetItem* it = itemForPath(dir)) it->setExpanded(true);
    emit foldersChanged({dir}, {});
}

void FolderTree::renameItem(QTreeWidgetItem* item) {
    const QString oldPath = pathOf(item);
    if (oldPath.isEmpty() || isVolumeRoot(oldPath)) return;
    const QString oldName = QFileInfo(oldPath).fileName();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("重命名"), QString::fromUtf8("新名称:"),
        QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || name == oldName) return;
    if (!isLegalFolderName(name)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
            QString::fromUtf8("名称不能包含 / \\ : * ? \" < > | 也不能为空"));
        return;
    }
    const QString parent = QFileInfo(oldPath).dir().absolutePath();
    const QString newPath = QDir(parent).filePath(name);
    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
            QString::fromUtf8("目标名已存在:\n") + newPath);
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名失败"), oldPath);
        return;
    }
    item->setText(0, name);
    item->setData(0, Qt::UserRole, newPath);
    // 子孙行的 UserRole 仍拼着旧前缀,留着就是一串坏路径。清成占位,
    // 下次展开按新前缀重新物化 —— 代价只有一次 readdir。
    while (item->childCount()) delete item->takeChild(0);
    if (hasVisibleSubdirs(newPath)) item->addChild(new QTreeWidgetItem);
    // removed 的语义是"这个目录没了,请离开":重命名不该把用户甩到父目录,
    // 路径迁移交给 folderRenamed 处理,避免先跳一次再重定向的二次加载。
    emit foldersChanged({parent, newPath}, {});
    emit folderRenamed(oldPath, newPath);
}

void FolderTree::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* hit = itemAt(pos);
    if (hit && !selectedItems().contains(hit)) {
        // 右键落在未选中的行上:选择集收缩到该行(资源管理器语义)
        clearSelection();
        setCurrentItem(hit);
        hit->setSelected(true);
    }
    if (!hit) hit = currentItem();   // 空白处:对当前选中项操作;没有就不弹
    if (!hit) return;

    QStringList paths = selectedPaths();
    if (paths.isEmpty()) paths << pathOf(hit);
    const QString base = pathOf(hit);      // 新建/粘贴的落点 = 光标下这一行
    const bool multi = paths.size() > 1;
    bool hasRoot = false;
    for (const auto& p : paths) if (isVolumeRoot(p)) hasRoot = true;

    QMenu menu(this);
    // ── 新建文件夹 ──
    menu.addAction(IconLib::appIcon("cmd_newFolder"), QString::fromUtf8("新建文件夹"),
                   this, [this, base]() { newFolderInto(itemForPath(base)); });
    menu.addSeparator();
    // ── 剪贴板组:盘符根不许剪切/复制(那等于要搬走整个卷) ──
    menu.addAction(IconLib::appIcon("cmd_cut"), QString::fromUtf8("剪切"),
                   this, [paths]() { clipboardSetFiles(paths, true); })
        ->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_copy"), QString::fromUtf8("复制"),
                   this, [paths]() { clipboardSetFiles(paths, false); })
        ->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_paste"), QString::fromUtf8("粘贴"),
                   this, [this, base]() { pasteInto(itemForPath(base)); })
        ->setEnabled(clipboardHasFiles());
    menu.addSeparator();
    // ── 删除:确认框/回收站/提示全部走 deleteWithSettings 这一条正门 ──
    menu.addAction(IconLib::appIcon("cmd_delete"), QString::fromUtf8("删除"),
                   this, [this, paths]() {
        if (!deleteWithSettings(paths, this)) return;
        removeNodes(paths);
        QStringList parents;
        for (const auto& p : paths) {
            const QString par = QFileInfo(p).dir().absolutePath();
            if (!par.isEmpty() && !parents.contains(par)) parents << par;
        }
        emit foldersChanged(parents, paths);
    })->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_rename"), QString::fromUtf8("重命名"),
                   this, [this, base]() { renameItem(itemForPath(base)); })
        ->setEnabled(!hasRoot && !multi);   // 多项改名语义不明,资源管理器同样禁用
    menu.addSeparator();
    // ── 复制到.. / 移动到...:整个选择集一起走 ──
    menu.addAction(IconLib::appIcon("cmd_copyTo"), QString::fromUtf8("复制到.."),
                   this, [this, paths]() {
        const QString dst = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("复制到.."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        const bool done = copyPathsTo(paths, dst, nullptr, &errs);
        reportErrors(errs, done ? QString::fromUtf8("部分项目未能复制")
                                : QString::fromUtf8("复制失败"));
        if (!done) return;
        refreshNode(dst);
        emit foldersChanged({dst}, {});
    })->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("min_moveTo"), QString::fromUtf8("移动到..."),
                   this, [this, paths]() {
        const QString dst = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("移动到..."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        const bool done = movePathsTo(paths, dst, nullptr, &errs);
        reportErrors(errs, done ? QString::fromUtf8("部分项目未能移动")
                                : QString::fromUtf8("移动失败"));
        if (!done) return;
        // 源那一层少了条目,目的那一层多了条目:两端都要对齐磁盘
        QStringList changed;
        for (const auto& p : paths) {
            const QString par = QFileInfo(p).dir().absolutePath();
            if (!par.isEmpty() && !changed.contains(par)) changed << par;
        }
        changed << dst;
        for (const QString& d : changed) refreshNode(d);
        emit foldersChanged(changed, {});
    })->setEnabled(!hasRoot);
    menu.addSeparator();
    // ── 显示子文件夹中的文件:开关的真源在网格,这里只镜像勾状态 ──
    QAction* sub = menu.addAction(IconLib::appIcon("cmd_showFilesInFolder"),
                                  QString::fromUtf8("显示子文件夹中的文件"));
    sub->setCheckable(true);
    sub->setChecked(m_subFoldersShown);
    connect(sub, &QAction::triggered, this, [this](bool on) {
        m_subFoldersShown = on;
        emit subFoldersToggled(on);
    });
    menu.addSeparator();
    // ── 搜索...:以光标下这一层为根的递归名称搜索(非模态,可边搜边看主窗口) ──
    menu.addAction(IconLib::appIcon("cmd_search"), QString::fromUtf8("搜索..."),
                   this, [this, base]() {
        (new SearchDialog(base, window()))->show();
    });
    // ── 用资源管理器打开:交给 Shell,尊重第三方文件管理器的接管 ──
    menu.addAction(IconLib::appIcon("cmd_browse"),
                   QString::fromUtf8("用资源管理器打开文件"), this, [paths]() {
        for (const auto& p : paths)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    menu.addAction(IconLib::appIcon("cmd_openProperties"), QString::fromUtf8("属性"),
                   this, [base]() { showShellProperties(base); });

    menu.exec(viewport()->mapToGlobal(pos));
}

// ═══════════════════════════════════════════
// 右键菜单 —— 操作对象是"文件夹"本身(网格那份操作的是文件)
//   新建文件夹 / 剪切 / 复制 / 粘贴 / 删除 / 重命名 / 复制到.. / 移动到...
//   显示子文件夹中的文件 / 用资源管理器打开文件 / 属性
//   ("搜索..." 需要独立的搜索对话框,尚未接线,不放空项骗人)
// ═══════════════════════════════════════════

QString FolderTree::pathOf(const QTreeWidgetItem* item) {
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

bool FolderTree::isVolumeRoot(const QString& path) {
    return !path.isEmpty() && QDir(path).isRoot();
}

// 树里只物化了"展开过的那些行",所以这里查的是已存在的节点;查不到通常意味着
// 该目录在某条折叠分支下面 —— 那也正是不需要维护的情况(展开时会重扫)
QTreeWidgetItem* FolderTree::itemForPath(const QString& path) const {
    if (path.isEmpty()) return nullptr;
    const QString want = QDir::cleanPath(path);
    for (QTreeWidgetItemIterator it(this); *it; ++it) {
        const QString p = pathOf(*it);
        if (p.isEmpty()) continue;           // 懒加载占位行不算命中
        if (QDir::cleanPath(p).compare(want, Qt::CaseInsensitive) == 0) return *it;
    }
    return nullptr;
}

QStringList FolderTree::selectedPaths() const {
    QStringList out;
    const QList<QTreeWidgetItem*> items = selectedItems();
    for (const QTreeWidgetItem* it : items) {
        const QString p = pathOf(it);
        if (!p.isEmpty()) out << p;
    }
    return out;
}

// 某一层结构变了(新建/粘贴/移入/移出/改名),让树上的这一层重新对齐磁盘。
// 未物化或折叠着的分支一律退回"占位":展开时必然重扫,既正确又不白扫。
void FolderTree::refreshNode(const QString& dirPath) {
    QTreeWidgetItem* it = itemForPath(dirPath);
    if (!it) return;
    const bool placeholder =
        (it->childCount() == 1 && it->child(0)->text(0).isEmpty());
    if (placeholder || !it->isExpanded()) {
        while (it->childCount()) delete it->takeChild(0);
        if (hasVisibleSubdirs(dirPath)) it->addChild(new QTreeWidgetItem);
        return;
    }
    while (it->childCount()) delete it->takeChild(0);
    loadChildren(it);
}

void FolderTree::removeNodes(const QStringList& paths) {
    for (const QString& p : paths) {
        QTreeWidgetItem* it = itemForPath(p);
        if (!it) continue;
        if (QTreeWidgetItem* par = it->parent()) par->removeChild(it);
        else removeTopLevelItem(it);
        delete it;   // removeChild 只是摘出树,所有权仍在调用方
    }
}

void FolderTree::reportErrors(const QStringList& errors, const QString& title) {
    if (errors.isEmpty()) return;
    QString text = errors.join(QLatin1Char('\n'));
    if (errors.size() > 8)
        text = errors.mid(0, 8).join(QLatin1Char('\n'))
             + QString::fromUtf8("\n…另有 %1 条").arg(errors.size() - 8);
    QMessageBox::warning(this, title, text);
}

// Windows 目录名禁区。不接受分隔符是硬要求:旧实现把用户输入直接拼进路径,
// 一个 "a/b" 就能让"重命名"把整个文件夹搬到别处去(表面上什么都没发生)。
static bool isLegalFolderName(const QString& name) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name)
        if (bad.contains(c) || c.unicode() < 0x20) return false;
    return true;
}

// Windows 目录名禁区。不接受分隔符是硬要求:旧实现把用户输入直接拼进路径,
// 一个 "a/b" 就能让"重命名"把整个文件夹搬到别处去(表面上什么都没发生)。
static bool isLegalFolderName(const QString& name) {
    if (name.isEmpty() || name == QLatin1String(".") || name == QLatin1String(".."))
        return false;
    static const QString bad = QStringLiteral("/\\:*?\"<>|");
    for (const QChar c : name)
        if (bad.contains(c) || c.unicode() < 0x20) return false;
    return true;
}

void FolderTree::newFolderInto(QTreeWidgetItem* base) {
    const QString dir = pathOf(base);
    if (dir.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("新建文件夹"), QString::fromUtf8("名称:"),
        QLineEdit::Normal, QString::fromUtf8("新建文件夹"), &ok).trimmed();
    if (!ok) return;
    if (!isLegalFolderName(name)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("名称不能包含 / \\ : * ? \" < > | 也不能为空"));
        return;
    }
    const QString full = QDir(dir).filePath(name);
    if (QFileInfo::exists(full)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("同名文件夹已存在:\n") + full);
        return;
    }
    if (!QDir().mkdir(full)) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"),
            QString::fromUtf8("创建失败:\n") + full);
        return;
    }
    refreshNode(dir);
    if (QTreeWidgetItem* it = itemForPath(dir)) it->setExpanded(true);
    emit foldersChanged({dir}, {});
}

void FolderTree::pasteInto(QTreeWidgetItem* base) {
    const QString dir = pathOf(base);
    if (dir.isEmpty()) return;
    QStringList errs;
    const bool ok = clipboardPasteInto(QDir(dir), &errs);
    reportErrors(errs, ok ? QString::fromUtf8("部分项目未能粘贴")
                          : QString::fromUtf8("粘贴失败"));
    if (!ok) return;
    refreshNode(dir);
    if (QTreeWidgetItem* it = itemForPath(dir)) it->setExpanded(true);
    emit foldersChanged({dir}, {});
}

void FolderTree::renameItem(QTreeWidgetItem* item) {
    const QString oldPath = pathOf(item);
    if (oldPath.isEmpty() || isVolumeRoot(oldPath)) return;
    const QString oldName = QFileInfo(oldPath).fileName();
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("重命名"), QString::fromUtf8("新名称:"),
        QLineEdit::Normal, oldName, &ok).trimmed();
    if (!ok || name == oldName) return;
    if (!isLegalFolderName(name)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
            QString::fromUtf8("名称不能包含 / \\ : * ? \" < > | 也不能为空"));
        return;
    }
    const QString parent = QFileInfo(oldPath).dir().absolutePath();
    const QString newPath = QDir(parent).filePath(name);
    if (QFileInfo::exists(newPath)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"),
            QString::fromUtf8("目标名已存在:\n") + newPath);
        return;
    }
    if (!QFile::rename(oldPath, newPath)) {
        QMessageBox::warning(this, QString::fromUtf8("重命名失败"), oldPath);
        return;
    }
    item->setText(0, name);
    item->setData(0, Qt::UserRole, newPath);
    // 子孙行的 UserRole 仍拼着旧前缀,留着就是一串坏路径。清成占位,
    // 下次展开按新前缀重新物化 —— 代价只有一次 readdir。
    while (item->childCount()) delete item->takeChild(0);
    if (hasVisibleSubdirs(newPath)) item->addChild(new QTreeWidgetItem);
    emit foldersChanged({parent, newPath}, {oldPath});
}

void FolderTree::showContextMenu(const QPoint& pos) {
    QTreeWidgetItem* hit = itemAt(pos);
    if (hit && !selectedItems().contains(hit)) {
        // 右键落在未选中的行上:选择集收缩到该行(资源管理器语义)
        clearSelection();
        setCurrentItem(hit);
        hit->setSelected(true);
    }
    if (!hit) hit = currentItem();   // 空白处:对当前选中项操作;没有就不弹
    if (!hit) return;

    QStringList paths = selectedPaths();
    if (paths.isEmpty()) paths << pathOf(hit);
    const QString base = pathOf(hit);      // 新建/粘贴的落点 = 光标下这一行
    const bool multi = paths.size() > 1;
    bool hasRoot = false;
    for (const auto& p : paths) if (isVolumeRoot(p)) hasRoot = true;

    QMenu menu(this);
    // ── 新建文件夹 ──
    menu.addAction(IconLib::appIcon("cmd_newFolder"), QString::fromUtf8("新建文件夹"),
                   this, [this, base]() { newFolderInto(itemForPath(base)); });
    menu.addSeparator();
    // ── 剪贴板组:盘符根不许剪切/复制(那等于要搬走整个卷) ──
    menu.addAction(IconLib::appIcon("cmd_cut"), QString::fromUtf8("剪切"),
                   this, [paths]() { clipboardSetFiles(paths, true); })
        ->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_copy"), QString::fromUtf8("复制"),
                   this, [paths]() { clipboardSetFiles(paths, false); })
        ->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_paste"), QString::fromUtf8("粘贴"),
                   this, [this, base]() { pasteInto(itemForPath(base)); })
        ->setEnabled(clipboardHasFiles());
    menu.addSeparator();
    // ── 删除:确认框/回收站/提示全部走 deleteWithSettings 这一条正门 ──
    menu.addAction(IconLib::appIcon("cmd_delete"), QString::fromUtf8("删除"),
                   this, [this, paths]() {
        if (!deleteWithSettings(paths, this)) return;
        removeNodes(paths);
        QStringList parents;
        for (const auto& p : paths) {
            const QString par = QFileInfo(p).dir().absolutePath();
            if (!par.isEmpty() && !parents.contains(par)) parents << par;
        }
        emit foldersChanged(parents, paths);
    })->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("cmd_rename"), QString::fromUtf8("重命名"),
                   this, [this, base]() { renameItem(itemForPath(base)); })
        ->setEnabled(!hasRoot && !multi);   // 多项改名语义不明,资源管理器同样禁用
    menu.addSeparator();
    // ── 复制到.. / 移动到...:整个选择集一起走 ──
    menu.addAction(IconLib::appIcon("cmd_copyTo"), QString::fromUtf8("复制到.."),
                   this, [this, paths]() {
        const QString dst = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("复制到.."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        const bool done = copyPathsTo(paths, dst, nullptr, &errs);
        reportErrors(errs, done ? QString::fromUtf8("部分项目未能复制")
                                : QString::fromUtf8("复制失败"));
        if (!done) return;
        refreshNode(dst);
        emit foldersChanged({dst}, {});
    })->setEnabled(!hasRoot);
    menu.addAction(IconLib::appIcon("min_moveTo"), QString::fromUtf8("移动到..."),
                   this, [this, paths]() {
        const QString dst = QFileDialog::getExistingDirectory(
            this, QString::fromUtf8("移动到..."), QString());
        if (dst.isEmpty()) return;
        QStringList errs;
        const bool done = movePathsTo(paths, dst, nullptr, &errs);
        reportErrors(errs, done ? QString::fromUtf8("部分项目未能移动")
                                : QString::fromUtf8("移动失败"));
        if (!done) return;
        // 源那一层少了条目,目的那一层多了条目:两端都要对齐磁盘
        QStringList changed;
        for (const auto& p : paths) {
            const QString par = QFileInfo(p).dir().absolutePath();
            if (!par.isEmpty() && !changed.contains(par)) changed << par;
        }
        changed << dst;
        for (const QString& d : changed) refreshNode(d);
        emit foldersChanged(changed, {});
    })->setEnabled(!hasRoot);
    menu.addSeparator();
    // ── 显示子文件夹中的文件:开关的真源在网格,这里只镜像勾状态 ──
    QAction* sub = menu.addAction(IconLib::appIcon("cmd_showFilesInFolder"),
                                  QString::fromUtf8("显示子文件夹中的文件"));
    sub->setCheckable(true);
    sub->setChecked(m_subFoldersShown);
    connect(sub, &QAction::triggered, this, [this](bool on) {
        m_subFoldersShown = on;
        emit subFoldersToggled(on);
    });
    menu.addSeparator();
    // ── 用资源管理器打开:交给 Shell,尊重第三方文件管理器的接管 ──
    menu.addAction(IconLib::appIcon("cmd_browse"),
                   QString::fromUtf8("用资源管理器打开文件"), this, [paths]() {
        for (const auto& p : paths)
            QDesktopServices::openUrl(QUrl::fromLocalFile(p));
    });
    menu.addAction(IconLib::appIcon("cmd_openProperties"), QString::fromUtf8("属性"),
                   this, [base]() { showShellProperties(base); });

    menu.exec(viewport()->mapToGlobal(pos));
}
