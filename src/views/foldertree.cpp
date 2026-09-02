#include "foldertree.h"
#include "constants.h"
#include "fileentry.h"
#include "namesort.h"
#include "settings.h"
#include "perflog.h"
#include "iconlib.h"
#include "clipboardops.h"
#include "shelldelete.h"
#include "searchdialog.h"
#include "validname.h"
#include "dialogs/renamedialog.h"   // 2026-09-02:文件重命名对话框(仿 XnView 带插入日期/时间)

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
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QFileDialog>
#include <QMessageBox>
#include <QLineEdit>
#include <QDesktopServices>
#include <QUrl>
#include <QTreeWidgetItem>

#include <algorithm>
#include <utility>

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
            painter->setBrush(QColor(C_TEXT));   // 箭头随主题取色,常显,可展开处一目了然
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
// 只要"有一个子文件夹"就够:见到第一个即收工。旧写法 entryList 会把整层枚举完
// 并分配 QStringList,而这一句对每个子行都要问一次 —— 是展开最贵的一笔账。
static bool hasVisibleSubdirs(const QString& path) {
    const QString pattern = path + QStringLiteral("\\*");
    WIN32_FIND_DATAW data;
    HANDLE h = FindFirstFileExW((const wchar_t*)pattern.utf16(), FindExInfoBasic, &data,
                                FindExSearchLimitToDirectories, nullptr,
                                FIND_FIRST_EX_LARGE_FETCH);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool found = false;
    do {
        const wchar_t* n = data.cFileName;
        if (n[0] == L'.' && (n[1] == 0 || (n[1] == L'.' && n[2] == 0))) continue;
        // 部分文件系统不理会"只要目录"的下推,自己按属性位兜底
        if (data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) { found = true; break; }
    } while (FindNextFileW(h, &data));
    FindClose(h);
    return found;
}

FolderTree::FolderTree(QWidget* parent) : QTreeWidget(parent) {
    setHeaderHidden(true);
    setIndentation(16);
    // 与 XnView 一致：内容少于一页也保留竖向滚动条，整条长拇指表示不可拖动
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
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
    // 用户确认不需要展开/收起动画：保持即时展开
    setAnimated(false);

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

    // #117:左键按住拖动的语义(0=扫过即切入,1=拖动多选)
    m_sweepSwitch =
        AppSettings::instance().get("FolderTree/leftDragSweep", 0).toInt() == 0;
    connect(&AppSettings::instance(), &AppSettings::changed, this, [this]() {
        m_sweepSwitch =
            AppSettings::instance().get("FolderTree/leftDragSweep", 0).toInt() == 0;
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
    // cleanPath:"E:/" 这类带尾斜杠的盘符行会让 fastScanDir 拼出 "E://name",
    // 而旧实现 absoluteFilePath() 从不产生双斜杠 —— 路径分隔符数量不一致会
    // 让下游的前缀比较(canonicalPath/focusPath)整段失配。
    const QString path = QDir::cleanPath(item->data(0, Qt::UserRole).toString());
    // 一次 FindFirstFileExW 扫描取全 name/path/属性:旧写法 entryInfoList 要为
    // 每个条目建 QFileInfo(名称拆分、缓存、绝对路径再走一遍字符串加工),
    // 目录行数多的时候这笔账全部落在展开的那一帧上。
    auto raw = fastScanDir(path);
    std::vector<FileEntry> dirs;
    dirs.reserve(raw.size());
    for (auto& fe : raw)
        if (fe.isDir) dirs.push_back(std::move(fe));
    // 数字顺序:1 < 2 < 10(#98)。旧的纯忽略大小写比较等于 QDir::Name,
    // 编号文件夹(2022-12-28、1、2、10…)会排成 1,10,2,与网格默认序打架
    std::sort(dirs.begin(), dirs.end(), [](const FileEntry& a, const FileEntry& b) {
        return naturalNameLess(a.name, b.name);
    });

    for (const FileEntry& fe : dirs) {
        auto* child = new QTreeWidgetItem;
        child->setText(0, fe.name);
        // 隐藏文件夹用淡灰文字 + 半透明图标；普通文件夹正常
        child->setIcon(0, fe.hidden ? m_folderIconDim : m_folderIcon);
        child->setData(0, Qt::UserRole, fe.path);
        child->setForeground(0, QBrush(QColor(fe.hidden ? C_TEXT_HIDDEN : C_TREE_TEXT)));
        // 检测是否有子文件夹（含隐藏）；没有就不留展开按钮
        if (hasVisibleSubdirs(fe.path))
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

// ── #117 左键按住扫过 = 切换文件夹(仅 leftDragSweep=0 时)──
// #130:跳转发生在**左键按下的那一刻**,不是松开时(原来要等 itemClicked,
// 松开才切,扫动时手感是"拖过一堆目录,松手才跳一个")。
// 按下即切与扫过模式**解耦**(2026-09-02 用户复验仍不立刻跳,根因在此):
// 无论 leftDragSweep 取 0/1,单击按下就切 —— 这是 #130 的基本承诺;
// 扫过只是"按住拖动掠过其他目录"时是否也跟着切(拖动多选档禁用它)。
// 展开箭头那一列(分支槽 + 其左侧)按下只做展开/收起,不切目录 —— 否则
// 用户点"+"想看子目录,主视图就被拽走了,这是资源管理器/浏览器都不有的行为。
void FolderTree::mousePressEvent(QMouseEvent* event) {
    m_sweepCur = itemAt(event->pos());
    if (event->button() == Qt::LeftButton && m_sweepCur) {
        int depth = 0;                       // 层级:QTreeWidgetItem 没有 depth(),自己数
        for (QTreeWidgetItem* p = m_sweepCur->parent(); p; p = p->parent()) ++depth;
        const int branchRight = visualRect(indexFromItem(m_sweepCur)).left()
                              + (depth + 1) * indentation();
        const QString path = pathOf(m_sweepCur);
        if (!path.isEmpty() && event->pos().x() >= branchRight) {
            m_pressActivated = path;
            setCurrentItem(m_sweepCur);
            emit folderSelected(path);       // 按下那一刻就切,不等到松开
        }
    }
    QTreeWidget::mousePressEvent(event);
}

void FolderTree::mouseMoveEvent(QMouseEvent* event) {
    if (m_sweepSwitch && (event->buttons() & Qt::LeftButton)) {
        if (QTreeWidgetItem* it = itemAt(event->pos()); it && it != m_sweepCur) {
            const QString path = pathOf(it);
            if (!path.isEmpty()) {          // 占位行/空白:交回基类,不瞎切
                m_sweepCur = it;
                m_pressActivated = path;
                setCurrentItem(it);
                emit folderSelected(path);
                event->accept();
                return;
            }
        }
    }
    QTreeWidget::mouseMoveEvent(event);
}

void FolderTree::mouseReleaseEvent(QMouseEvent* event) {
    m_sweepCur = nullptr;
    QTreeWidget::mouseReleaseEvent(event);
    m_pressActivated.clear();   // 基类已在本次松开里发过 itemClicked(已被它消费)
}

void FolderTree::focusPath(const QString& dirPath) {
    const QString want = QDir::cleanPath(dirPath);
    if (want.isEmpty()) return;
    // 树上点出来的导航不该再走一遍定位:先看一眼现答案
    if (QTreeWidgetItem* cur = currentItem())
        if (QDir::cleanPath(pathOf(cur)).compare(want, Qt::CaseInsensitive) == 0) return;
    PerfLog::Scope scope("focusPath", 5);

    // 占位行(懒加载标记)→ 真子行;已物化的不动
    auto materialize = [this](QTreeWidgetItem* it) {
        if (it->childCount() == 1 && it->child(0)->text(0).isEmpty()) {
            delete it->takeChild(0);
            loadChildren(it);
        }
    };

    // 顶层起点取"最长前缀"匹配:桌面本身也在 C:/Users/… 下,取最贴的一行
    QTreeWidgetItem* node = nullptr;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        QTreeWidgetItem* top = topLevelItem(i);
        const QString tp = QDir::cleanPath(pathOf(top));
        if (tp.isEmpty()) continue;
        const QString prefix = tp.endsWith(QLatin1Char('/')) ? tp : tp + QLatin1Char('/');
        const bool hit = tp.compare(want, Qt::CaseInsensitive) == 0
                      || want.startsWith(prefix, Qt::CaseInsensitive);
        if (hit && (!node || tp.size() > QDir::cleanPath(pathOf(node)).size())) node = top;
    }
    if (!node) return;

    while (node) {
        const QString np = QDir::cleanPath(pathOf(node));
        if (np.compare(want, Qt::CaseInsensitive) == 0) break;
        const QString rest = want.mid(np.endsWith(QLatin1Char('/')) ? np.size() : np.size() + 1);
        const QString head = rest.section(QLatin1Char('/'), 0, 0);
        if (head.isEmpty()) break;
        materialize(node);
        QTreeWidgetItem* next = nullptr;
        for (int i = 0; i < node->childCount(); ++i) {
            QTreeWidgetItem* c = node->child(i);
            const QString cp = QDir::cleanPath(pathOf(c));
            if (!cp.isEmpty() && QFileInfo(cp).fileName().compare(head, Qt::CaseInsensitive) == 0) {
                next = c;
                break;
            }
        }
        if (!next) break;      // 这一层树上没有(隐藏规则外/网络路径):停在最深真实行
        node->setExpanded(true);
        node = next;
    }
    setCurrentItem(node);
    scrollToItem(node, QAbstractItemView::PositionAtCenter);
}

void FolderTree::onItemClicked(QTreeWidgetItem* item, int /*column*/) {
    const QString path = item ? item->data(0, Qt::UserRole).toString() : QString();
    // #130:按下那一刻已经切过这个目录 → 松开时基类补发的这次 itemClicked 不能再切
    // 一遍(两次导航会把刚铺好的滚动/选中状态打断)。消费掉这个标记即可。
    if (!path.isEmpty() && path == m_pressActivated) {
        m_pressActivated.clear();
        return;
    }
    emit folderSelected(path);
}

// ═══════════════════════════════════════════
// 右键菜单 —— 操作对象是"文件夹"本身(网格那份操作的是文件)
//   新建文件夹 / 剪切 / 复制 / 粘贴 / 删除 / 重命名 / 复制到.. / 移动到...
//   显示子文件夹中的文件 / 搜索... / 用资源管理器打开文件 / 属性
// ═══════════════════════════════════════════

QString FolderTree::pathOf(const QTreeWidgetItem* item) {
    return item ? item->data(0, Qt::UserRole).toString() : QString();
}

// 拖放(#81):落点 → 该行代表的目录路径(空白/非目录返回空)
QString FolderTree::pathAt(const QPoint& pos) const {
    QTreeWidgetItem* it = itemAt(pos);
    if (!it) return {};
    const QString p = it->data(0, Qt::UserRole).toString();
    return QFileInfo(p).isDir() ? p : QString();
}

// 结构变化后刷新当前行(拖入复制完成后用)
void FolderTree::refreshCurrent() {
    if (QTreeWidgetItem* it = currentItem()) {
        const QString p = it->data(0, Qt::UserRole).toString();
        if (!p.isEmpty()) refreshNode(p);
    }
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

void FolderTree::newFolderInto(QTreeWidgetItem* base) {
    const QString dir = pathOf(base);
    if (dir.isEmpty()) return;
    bool ok = false;
    const QString name = QInputDialog::getText(
        this, QString::fromUtf8("新建文件夹"), QString::fromUtf8("名称:"),
        QLineEdit::Normal, QString::fromUtf8("新建文件夹"), &ok).trimmed();
    if (!ok) return;
    if (const QString why = invalidNameReason(name); !why.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("新建文件夹"), why);
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

// #136:F2/F3 的键盘入口。守卫与右键那条**同源**：空行、盘符根、多选都静默不动
// （右键那三项是 setEnabled(false)，键盘没有"置灰"可看，只能什么都不做）。
// 改名逻辑仍然只有 renameItem 一份 —— 不要再抄第二份，早晚不同步。
void FolderTree::renameSelected() {
    QTreeWidgetItem* it = currentItem();
    if (!it) return;
    const QString p = pathOf(it);
    if (p.isEmpty() || isVolumeRoot(p)) return;
    if (selectedPaths().size() > 1) return;
    renameItem(it);
}

void FolderTree::renameItem(QTreeWidgetItem* item) {
    const QString oldPath = pathOf(item);
    if (oldPath.isEmpty() || isVolumeRoot(oldPath)) return;
    const QString oldName = QFileInfo(oldPath).fileName();
    // 2026-09-02:与文件页共用仿 XnView 的重命名对话框(带插入日期/时间)
    const QString name = RenameDialog::getName(this, oldName);
    if (name.isEmpty() || name == oldName) return;
    if (const QString why = invalidNameReason(name); !why.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("重命名"), why);
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
