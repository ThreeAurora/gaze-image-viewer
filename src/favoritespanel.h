#pragma once
// ═══════════════════════════════════════════
// 收藏夹面板(任务 #243)
//   · 条目 = 用户右键"添加到收藏夹"攒下的路径(目录与文件都收),
//     数据真源在 MainWindow(Favorites/paths),面板只负责显示与交互。
//   · 双击:目录 = 进入;文件 = 打开所在目录并选中(与地址栏跳转同一套链)。
//   · 右键:打开 / 在浏览器中定位 / 在资源管理器中显示 / 移除 / 清空。
// ═══════════════════════════════════════════
#include <QWidget>
#include <QVBoxLayout>
#include <QListWidget>
#include <QListWidgetItem>
#include <QFileInfo>
#include <QMenu>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>
#include "constants.h"
#include "i18n.h"

class FavoritesPanel : public QWidget {
    Q_OBJECT
public:
    explicit FavoritesPanel(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_list = new QListWidget;
        m_list->setFrameShape(QFrame::NoFrame);
        m_list->setUniformItemSizes(false);
        m_list->setTextElideMode(Qt::ElideMiddle);
        m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        connect(m_list, &QListWidget::itemDoubleClicked, this,
                [this](QListWidgetItem* it) {
            if (!it) return;
            const QString p = it->data(Qt::UserRole).toString();
            if (!p.isEmpty()) emit openRequested(p);
        });
        m_list->setContextMenuPolicy(Qt::CustomContextMenu);
        connect(m_list, &QWidget::customContextMenuRequested, this,
                [this](const QPoint& pos) {
            QListWidgetItem* hit = m_list->itemAt(pos);
            if (!hit) return;                       // 空白处不弹(清空走条目菜单也行,但空表没有条目)
            const QString p = hit->data(Qt::UserRole).toString();
            if (p.isEmpty()) return;                // 占位提示行不带路径
            QMenu menu(this);
            menu.addAction(gazeTr("打开"), this, [this, p]() { emit openRequested(p); });
            menu.addAction(gazeTr("在浏览器中定位"), this, [this, p]() { emit locateRequested(p); });
            menu.addAction(gazeTr("在资源管理器中显示"), this, [p]() {
                QDesktopServices::openUrl(QUrl::fromLocalFile(
                    QFileInfo(p).isDir() ? p : QFileInfo(p).absolutePath()));
            });
            menu.addSeparator();
            menu.addAction(gazeTr("从收藏夹移除"), this, [this, p]() { emit removeRequested(p); });
            menu.addAction(gazeTr("清空收藏夹"), this, [this]() { emit clearRequested(); });
            menu.exec(m_list->mapToGlobal(pos));
        });
        root->addWidget(m_list, 1);
        // 列表样式在应用级 QSS(QListWidget#favList,#89 收敛):切主题由
        // applyLive 重设全局表自动跟上,不再需要 #248 重灌钩子
        m_list->setObjectName(QStringLiteral("favList"));
    }

    // 真源刷新:整表重建(收藏量级是个位数~几十,不设增量 diff)
    void setPaths(const QStringList& paths) {
        m_list->clear();
        if (paths.isEmpty()) {
            // 空态:一行不可选的提示,别让面板白板一块
            auto* hint = new QListWidgetItem(gazeTr(
                "空。右键文件或文件夹 → 添加到收藏夹"));
            hint->setFlags(Qt::NoItemFlags);
            m_list->addItem(hint);
            return;
        }
        for (const QString& p : paths) {
            const QFileInfo fi(p);
            // 盘根的 fileName 为空("C:/" → ""),退回整段路径
            const QString name = fi.fileName().isEmpty() ? p : fi.fileName();
            auto* it = new QListWidgetItem(name);
            it->setData(Qt::UserRole, p);
            it->setToolTip(p);
            if (fi.isDir()) {
                QFont f = it->font();
                f.setBold(true);
                it->setFont(f);
            }
            m_list->addItem(it);
        }
    }

signals:
    void openRequested(const QString& path);     // 双击/菜单"打开"
    void locateRequested(const QString& path);   // 菜单"在浏览器中定位"
    void removeRequested(const QString& path);
    void clearRequested();

private:
    QListWidget* m_list = nullptr;
};
