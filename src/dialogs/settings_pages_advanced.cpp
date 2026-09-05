#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"
#include "imgsearch.h"
#include "i18n.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFileInfo>
#include <QMessageBox>
#include <QDesktopServices>
#include <QUrl>
#include <QTableWidget>
#include <QHeaderView>
#include <QKeySequenceEdit>
#include <QMenuBar>
#include <QScrollArea>
#include <QFrame>
#include <QTreeWidget>
#include <QListWidget>
#include <QPalette>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStyledItemDelegate>
#include <QStyleOptionViewItem>
#include <QToolButton>
#include <QMenu>
#include <QColorDialog>
#include <QRadioButton>
#include <QRegularExpression>
#include <QSet>
#include <functional>
#include "dbmaintenance.h"

// 窄列中段省略(C:\Use...der\ 形式;列宽可拖动配合)
class ElideMiddleDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;
    void initStyleOption(QStyleOptionViewItem* o, const QModelIndex& i) const override {
        QStyledItemDelegate::initStyleOption(o, i);
        o->textElideMode = Qt::ElideMiddle;
    }
};

// ── 维护页:缩略图库统计 + 四列目录表(可拖列宽/中段省略) + 操作按钮 ──
QWidget* SettingsDialog::pageMaintenance() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);

    // 数据库统计行
    auto* summary = new QLabel;
    summary->setObjectName(QStringLiteral("settingsDbSummary"));
    root->addWidget(summary);

    // 筛选框
    auto* filter = new QLineEdit;
    filter->setPlaceholderText(gazeTr("筛选"));
    root->addWidget(filter);

    // 四列目录表:列宽可拖动,窄列中段省略
    auto* table = new QTableWidget(0, 4);
    table->setHorizontalHeaderLabels({gazeTr("缓存目录"),
        gazeTr("文件"), gazeTr("元数据"),
        gazeTr("缩略图")});
    table->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    table->horizontalHeader()->setStretchLastSection(true);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setItemDelegate(new ElideMiddleDelegate(table));
    // 表样式在应用级 QSS(QTableWidget#settingsTable,#89 收敛,快捷键页同款)
    table->setObjectName(QStringLiteral("settingsTable"));
    table->setColumnWidth(0, 260);
    table->setColumnWidth(1, 90);
    table->setColumnWidth(2, 110);
    root->addWidget(table, 1);

    // 按钮组(两行)
    auto* row1 = new QHBoxLayout;
    row1->setSpacing(8);
    auto* delSelBtn = new QPushButton(gazeTr("删除"));
    auto* maintBtn = new QPushButton(gazeTr("维护..."));
    auto* syncBtn = new QPushButton(gazeTr("同步文件夹..."));
    row1->addWidget(delSelBtn);
    row1->addStretch();
    row1->addWidget(maintBtn);
    row1->addWidget(syncBtn);
    root->addLayout(row1);

    auto* row2 = new QHBoxLayout;
    row2->setSpacing(8);
    auto* delAllBtn = new QPushButton(gazeTr("删除全部"));
    auto* rebuildBtn = new QPushButton(gazeTr("重建缩略图"));
    row2->addWidget(delAllBtn);
    row2->addStretch();
    row2->addWidget(rebuildBtn);
    root->addLayout(row2);

    // ── 数据访问(thumbs 表:key/png/mtime/atime;独立连接名) ──
    auto db = []() -> QSqlDatabase {
        const QString conn = QStringLiteral("settings_maint_db");
        if (!QSqlDatabase::contains(conn)) {
            QSqlDatabase d = QSqlDatabase::addDatabase("QSQLITE", conn);
            d.setDatabaseName(AppSettings::instance().dataDir() + "/thumbnails.db");
            d.open();
        }
        return QSqlDatabase::database(conn);
    };

    auto reload = [db, summary, table, filter]() {
        QSqlDatabase d = db();
        struct Rec { int count = 0; qint64 meta = 0; qint64 thumb = 0; };
        QHash<QString, Rec> byDir;
        qint64 totalThumb = 0, totalMeta = 0;
        int total = 0;
        QSqlQuery q(d);
        // 元数据字节 = key 长度 + mtime/atime 两个 double(估算)
        if (q.exec("SELECT key, LENGTH(png), LENGTH(key) FROM thumbs")) {
            while (q.next()) {
                const QString p = q.value(0).toString();
                const qint64 thumbB = q.value(1).toLongLong();
                const qint64 metaB = q.value(2).toLongLong() + 16;
                totalThumb += thumbB;
                totalMeta += metaB;
                ++total;
                int slash = p.lastIndexOf('/');
                if (slash < 0) slash = p.lastIndexOf('\\');
                QString dir = slash > 0 ? p.left(slash + 1) : p;
                Rec& r = byDir[dir];
                r.count += 1;
                r.meta += metaB;
                r.thumb += thumbB;
            }
        }
        QFileInfo fi(d.databaseName());
        summary->setText(gazeTr(
            "数据库 [目录:%1  →  元数据:%2  →  缩略图:%3]")
            .arg(QString::asprintf("%.2f MB", (fi.size() + totalMeta) / 1048576.0))
            .arg(QString::asprintf("%.2f MB", totalMeta / 1048576.0))
            .arg(QString::asprintf("%.2f MB", totalThumb / 1048576.0)));

        QVector<QPair<QString, Rec>> rows;
        for (auto it = byDir.constBegin(); it != byDir.constEnd(); ++it)
            rows.append({it.key(), it.value()});
        std::sort(rows.begin(), rows.end(),
                  [](auto& a, auto& b) { return a.second.thumb > b.second.thumb; });

        const QString f = filter->text().trimmed();
        table->setRowCount(0);
        for (const auto& row : rows) {
            if (!f.isEmpty() && !row.first.contains(f, Qt::CaseInsensitive)) continue;
            int r = table->rowCount();
            table->insertRow(r);
            table->setItem(r, 0, new QTableWidgetItem(row.first));
            table->setItem(r, 1, new QTableWidgetItem(QString::number(row.second.count)));
            table->setItem(r, 2, new QTableWidgetItem(
                QString::asprintf("%.2f KB", row.second.meta / 1024.0)));
            table->setItem(r, 3, new QTableWidgetItem(
                QString::asprintf("%.2f KB", row.second.thumb / 1024.0)));
        }
    };

    connect(filter, &QLineEdit::textChanged, this, reload);
    connect(delSelBtn, &QPushButton::clicked, this, [this, table, db, reload]() {
        auto sel = table->selectedItems();
        if (sel.isEmpty()) return;
        QString dir = table->item(sel.first()->row(), 0)->text();
        if (QMessageBox::question(this, gazeTr("删除"),
            gazeTr("删除该目录的全部缓存条目?\n%1").arg(dir))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.prepare("DELETE FROM thumbs WHERE key LIKE ? ESCAPE '\\'");
        q.addBindValue(likePrefixPattern(dir));
        q.exec();
        reload();
    });
    connect(delAllBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, gazeTr("删除全部"),
            gazeTr("确认清空全部缩略图缓存?(浏览时会自动重建)"))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.exec("DELETE FROM thumbs");
        reload();
    });
    connect(rebuildBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, gazeTr("重建缩略图"),
            gazeTr("清空缓存后,下次浏览文件夹时将按当前设置自动重建缩略图。继续?"))
            != QMessageBox::Yes) return;
        QSqlQuery q(db());
        q.exec("DELETE FROM thumbs");
        reload();
    });
    // 同步文件夹:从库中删除"文件已不存在"的孤立条目
    connect(syncBtn, &QPushButton::clicked, this, [this, db, reload]() {
        if (QMessageBox::question(this, gazeTr("缓存数据库 - 同步目录"),
            gazeTr("警告!\n此操作将从缓存数据库中删除全部的孤立条目。\n是否继续?"),
            QMessageBox::Yes | QMessageBox::No)
            != QMessageBox::Yes) return;
        QSqlDatabase d = db();
        QStringList gone;
        QSqlQuery q(d);
        if (q.exec("SELECT key FROM thumbs")) {
            while (q.next()) {
                const QString p = q.value(0).toString();
                if (!QFileInfo::exists(p)) gone << p;
            }
        }
        d.transaction();
        QSqlQuery del(d);
        del.prepare("DELETE FROM thumbs WHERE key = ?");
        for (const auto& p : gone) { del.addBindValue(p); del.exec(); }
        d.commit();
        QMessageBox::information(this, gazeTr("同步目录"),
            gazeTr("已移除 %1 条孤立条目。").arg(gone.size()));
        reload();
    });
    // 维护...:优化数据库(VACUUM)/核对全目录/清除缩略图
    connect(maintBtn, &QPushButton::clicked, this, [this, db, reload]() {
        QDialog dlg(this);
        dlg.setWindowTitle(gazeTr("缓存维护"));
        dlg.setFixedWidth(320);
        auto* v = new QVBoxLayout(&dlg);
        auto* optChk = new QCheckBox(gazeTr("优化数据库(处理时间长)"));
        auto* lblClean = new QLabel(gazeTr("清理"));
        auto* scanChk = new QCheckBox(gazeTr("核对全目录(移除孤立条目)"));
        scanChk->setChecked(true);
        auto* lblPurge = new QLabel(gazeTr("清除"));
        auto* thumbChk = new QCheckBox(gazeTr("清除缩略图"));
        for (auto* w : std::vector<QWidget*>{ optChk, lblClean, scanChk, lblPurge, thumbChk }) {
            if (auto* c = qobject_cast<QCheckBox*>(w)) c->setMinimumHeight(24);
            v->addWidget(w);
        }
        auto* btns = new QHBoxLayout;
        btns->addStretch();
        auto* runBtn = new QPushButton(gazeTr("运行"));
        // 显式默认:否则 Enter 与"空格=确认"按**创建顺序**挑按钮(全靠 runBtn 恰好先建)
        runBtn->setDefault(true);
        auto* cancelBtn = new QPushButton(gazeTr("取消"));
        btns->addWidget(runBtn);
        btns->addWidget(cancelBtn);
        v->addLayout(btns);
        connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
        connect(runBtn, &QPushButton::clicked, &dlg, [&]() {
            QSqlDatabase d = db();
            if (optChk->isChecked()) {
                QSqlQuery q(d);
                q.exec("VACUUM");
            }
            if (scanChk->isChecked()) {
                QStringList gone;
                QSqlQuery q(d);
                if (q.exec("SELECT key FROM thumbs")) {
                    while (q.next()) {
                        const QString p = q.value(0).toString();
                        if (!QFileInfo::exists(p)) gone << p;
                    }
                }
                d.transaction();
                QSqlQuery del(d);
                del.prepare("DELETE FROM thumbs WHERE key = ?");
                for (const auto& p : gone) { del.addBindValue(p); del.exec(); }
                d.commit();
            }
            if (thumbChk->isChecked()) {
                QSqlQuery q(d);
                q.exec("DELETE FROM thumbs");
            }
            dlg.accept();
            reload();
        });
        dlg.exec();
    });

    reload();
    return wrapTitled(gazeTr("维护"), root);
}

QWidget* SettingsDialog::pageIntegration() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距)

    // 分组"右键菜单"
    auto* fMenu = new QFormLayout;
    fMenu->setVerticalSpacing(6);
    auto* browseChk = new QCheckBox(
        gazeTr("将\"用 Gaze 浏览\"添加到系统右键菜单(HKCU,免管理员)"));
    browseChk->setChecked(Integration::isBrowseMenuInstalled());
    connect(browseChk, &QCheckBox::toggled, this, [](bool on) {
        bool ok = on ? Integration::addBrowseContextMenu()
                     : Integration::removeBrowseContextMenu();
        QMessageBox::information(nullptr, gazeTr("系统集成"),
            ok ? (on ? gazeTr("已添加右键菜单,资源管理器中即时生效。")
                     : gazeTr("已移除右键菜单。"))
               : gazeTr("注册表写入失败。"));
    });
    fMenu->addRow(browseChk);
    fMenu->addRow(chk("Integration/shellMenu",
        gazeTr("添加 shell 至右键菜单"), true));
    root->addWidget(group(gazeTr("右键菜单"), fMenu));

    // 分组"文件关联"(醒目大按钮)
    auto* fAssoc = new QFormLayout;
    fAssoc->setVerticalSpacing(6);
    auto* regBtn = new QPushButton(gazeTr("注册应用(加入\"打开方式\"列表)"));
    connect(regBtn, &QPushButton::clicked, this, []() {
        bool ok = Integration::registerOpenWith();
        QMessageBox::information(nullptr, gazeTr("注册应用"),
            ok ? gazeTr("已注册。右键文件 → 打开方式 中可选 Gaze。")
               : gazeTr("注册失败。"));
    });
    fAssoc->addRow(regBtn);
    // #204 文件关联:ProgId+应用能力+逐扩展名登记,一步到位。
    // "真正的默认"(UserChoice)带系统哈希,程序不可直写 —— 注册后走
    // 系统设置→默认应用→Gaze→"设为默认",由 Windows 落笔,这是官方正路。
    auto* assocBtn = new QPushButton(gazeTr("注册文件关联(图片+RAW 全部扩展名)"));
    connect(assocBtn, &QPushButton::clicked, this, []() {
        bool ok = Integration::registerFileAssociations();
        QMessageBox::information(nullptr, gazeTr("文件关联"),
            ok ? gazeTr("已注册。任意图片右键→\"打开方式\"可选 Gaze;\n系统设置→应用→默认应用→Gaze→\"设为默认\"一键绑定全部类型。")
               : gazeTr("注册文件关联失败(注册表写入被拒)。"));
    });
    fAssoc->addRow(assocBtn);
    auto* unassocBtn = new QPushButton(gazeTr("移除文件关联"));
    connect(unassocBtn, &QPushButton::clicked, this, []() {
        bool ok = Integration::removeFileAssociations();
        QMessageBox::information(nullptr, gazeTr("文件关联"),
            ok ? gazeTr("已移除 Gaze 的全部文件关联登记。")
               : gazeTr("移除失败(注册表写入被拒)。"));
    });
    fAssoc->addRow(unassocBtn);
    auto* defAppBtn = new QPushButton(gazeTr("打开系统\"默认应用程序\"设置"));
    connect(defAppBtn, &QPushButton::clicked, this, []() {
        // #237:ms-settings: 是 URI 协议不是可执行文件,QProcess::startDetached
        // 起不来(静默失败);协议跳转的官方通路是 QDesktopServices::openUrl
        QDesktopServices::openUrl(QUrl("ms-settings:defaultapps"));
    });
    fAssoc->addRow(defAppBtn);
    // #237:媒体组(视频+音频)独立按钮 —— #204 当时刻意不绑媒体,同日用户主动要
    auto* mediaBtn = new QPushButton(gazeTr("注册文件关联(视频+音频 扩展名)"));
    connect(mediaBtn, &QPushButton::clicked, this, []() {
        bool ok = Integration::registerMediaFileAssociations();
        QMessageBox::information(nullptr, gazeTr("文件关联"),
            ok ? gazeTr("已注册。视频/音频右键→\"打开方式\"可选 Gaze;\n系统设置→应用→默认应用→Gaze→\"设为默认\"一并绑定媒体类型。")
               : gazeTr("注册文件关联失败(注册表写入被拒)。"));
    });
    fAssoc->addRow(mediaBtn);
    root->addWidget(group(gazeTr("文件关联"), fAssoc));

    // 分组"配置文件"
    auto* fIni = new QFormLayout;
    fIni->setVerticalSpacing(6);
    auto* iniCombo = new QComboBox;
    iniCombo->addItems({gazeTr("程序文件夹(便携)"),
                        gazeTr("系统文件夹 %APPDATA%(下次启动生效)"),
                        gazeTr("自定义...(下次启动生效)")});
    iniCombo->setCurrentIndex(qBound(0,
        AppSettings::instance().get("Integration/iniLocation", 0).toInt(), 2));
    connect(iniCombo, &QComboBox::currentIndexChanged, this,
            [iniCombo](int idx) {
                if (idx == 2) {
                    // 调用 Windows 原生资源管理器对话框选择目录
                    QString dir = QFileDialog::getExistingDirectory(
                        nullptr, gazeTr("选择配置文件目录"));
                    if (dir.isEmpty()) {   // 取消:回退到原选项,不写任何键
                        iniCombo->blockSignals(true);
                        iniCombo->setCurrentIndex(qBound(0,
                            AppSettings::instance().get("Integration/iniLocation", 0).toInt(), 2));
                        iniCombo->blockSignals(false);
                        return;
                    }
                    AppSettings::instance().set("Integration/customIniDir", dir);
                }
                AppSettings::instance().set("Integration/iniLocation", idx);
                // #122:值在改动当下已由 settings.cpp 复制到目标;换的是"下次启动读哪里",
                // 这句必须如实说明,别让人以为立刻搬家、也别让人以为要手动搬。
                const QString target = AppSettings::iniPathForLocation(
                    idx, AppSettings::instance().get("Integration/customIniDir").toString());
                QMessageBox::information(nullptr, gazeTr("配置文件"),
                    gazeTr("下次启动起,配置文件改用:\n%1\n\n"
                                      "当前设置已复制到该位置(目标已有文件时不覆盖)。")
                        .arg(QDir::toNativeSeparators(target)));
            });
    fIni->addRow(gazeTr("位置"), iniCombo);
    auto* iniPath = new QLineEdit(AppSettings::instance().iniPath());
    iniPath->setReadOnly(true);
    fIni->addRow(gazeTr("当前文件"), iniPath);
    root->addWidget(group(gazeTr("配置文件"), fIni));
    return wrapTitled(gazeTr("系统集成"), root);
}

// ── 以文搜图:万象图搜(imgseek)服务位置/生命周期/测试连接 ──
// 键:ImgSearch/dir python port autoStart killOnExit(消费方在 imgsearch.h 与
// mainwindow closeEvent;改动即时落 ini)
QWidget* SettingsDialog::pageImgSearch() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);

    // 分组"服务位置"
    auto* fLoc = new QFormLayout;
    fLoc->setVerticalSpacing(6);
    // 项目目录:输入 + 浏览 + main.py 存在性指示
    auto* dirEdit = edit("ImgSearch/dir", ImgSearch::defaultDir());
    auto* dirState = new QLabel;
    auto probeDir = [dirEdit, dirState] {
        const QString d = dirEdit->text().trimmed();
        const bool ok = !d.isEmpty() && QFileInfo::exists(d + "/main.py");
        dirState->setText(ok ? gazeTr("✓ 找到 main.py")
                             : gazeTr("✗ 未找到 main.py"));
        dirState->setStyleSheet(QString("background:transparent;color:%1;")
                                    .arg(ok ? "#7BC97B" : "#E07070"));
    };
    auto* dirBrowse = new QPushButton(gazeTr("浏览…"));
    connect(dirBrowse, &QPushButton::clicked, this, [this, dirEdit] {
        const QString d = QFileDialog::getExistingDirectory(
            this, gazeTr("选择万象图搜项目目录"), dirEdit->text());
        if (!d.isEmpty()) dirEdit->setText(d);
    });
    connect(dirEdit, &QLineEdit::textChanged, dirEdit, probeDir);
    probeDir();
    auto* dirRow = new QWidget;
    auto* dirLay = new QHBoxLayout(dirRow);
    dirLay->setContentsMargins(0, 0, 0, 0);
    dirLay->setSpacing(6);
    dirLay->addWidget(dirEdit, 1);
    dirLay->addWidget(dirBrowse);
    dirLay->addWidget(dirState);
    fLoc->addRow(gazeTr("项目目录"), dirRow);

    // Python 解释器:输入 + 浏览 + 存在性指示
    auto* pyEdit = edit("ImgSearch/python",
                        QStringLiteral("C:/miniconda3"));
    auto* pyState = new QLabel;
    auto probePy = [pyEdit, pyState] {
        const bool ok = QFileInfo::exists(pyEdit->text().trimmed());
        pyState->setText(ok ? gazeTr("✓ 存在")
                            : gazeTr("✗ 未找到"));
        pyState->setStyleSheet(QString("background:transparent;color:%1;")
                                   .arg(ok ? "#7BC97B" : "#E07070"));
    };
    auto* pyBrowse = new QPushButton(gazeTr("浏览…"));
    connect(pyBrowse, &QPushButton::clicked, this, [this, pyEdit] {
        const QString f = QFileDialog::getOpenFileName(
            this, gazeTr("选择 Python 解释器"), pyEdit->text(),
            gazeTr("可执行文件 (python*.exe)"));
        if (!f.isEmpty()) pyEdit->setText(QDir::toNativeSeparators(f));
    });
    connect(pyEdit, &QLineEdit::textChanged, pyEdit, probePy);
    probePy();
    auto* pyRow = new QWidget;
    auto* pyLay = new QHBoxLayout(pyRow);
    pyLay->setContentsMargins(0, 0, 0, 0);
    pyLay->setSpacing(6);
    pyLay->addWidget(pyEdit, 1);
    pyLay->addWidget(pyBrowse);
    pyLay->addWidget(pyState);
    fLoc->addRow(gazeTr("Python"), pyRow);

    fLoc->addRow(gazeTr("端口"),
                 spin("ImgSearch/port", 1024, 65535, 8747));
    auto* portNote = new QLabel(gazeTr(
        "与 imgseek 服务实际监听端口一致(默认 8747);服务已在运行时改动需重启服务。"));
    portNote->setObjectName(QStringLiteral("settingsNote"));
    fLoc->addRow(portNote);
    root->addWidget(group(gazeTr("服务位置"), fLoc));

    // 分组"服务生命周期"
    auto* fLife = new QFormLayout;
    fLife->setVerticalSpacing(6);
    // #222(2026-09-04 用户令):默认不自动拉起服务;开关只管"搜索时按需启动"
    fLife->addRow(chk("ImgSearch/autoStart",
        gazeTr("服务未运行时,搜索前自动启动万象图搜服务"), false));
    fLife->addRow(chk("ImgSearch/killOnExit",
        gazeTr("退出 Gaze 时结束由 Gaze 拉起的图搜服务"), false));
    auto* lifeNote = new QLabel(gazeTr(
        "默认由你手动运行 main.py;自动启动关闭时,以文搜图只会报\"服务未运行\"。"
        "\"退出时结束\"只回收由 Gaze 自动拉起的服务实例,手动启动的不受影响。"));
    lifeNote->setObjectName(QStringLiteral("settingsNote"));
    fLife->addRow(lifeNote);
    root->addWidget(group(gazeTr("服务生命周期"), fLife));

    // 分组"连接"
    auto* fTest = new QFormLayout;
    fTest->setVerticalSpacing(6);
    auto* testBtn = new QPushButton(gazeTr("测试连接"));
    auto* testState = new QLabel;
    connect(testBtn, &QPushButton::clicked, testState, [testBtn, testState] {
        testBtn->setEnabled(false);
        testState->setText(gazeTr("正在连接…"));
        testState->setStyleSheet(
            QString("background:transparent;color:%1;").arg(C_TEXT_FAINT));
        // ctx 挂在 testState:页销毁/重建后回调自动丢弃
        ImgSearch::pingAsync(testState, [testBtn, testState](bool alive) {
            testBtn->setEnabled(true);
            testState->setText(alive ? gazeTr("✓ 服务在线")
                                     : gazeTr("✗ 无法连接(服务未运行)"));
            testState->setStyleSheet(QString("background:transparent;color:%1;")
                                         .arg(alive ? "#7BC97B" : "#E07070"));
        });
    });
    fTest->addRow(testBtn, testState);
    root->addWidget(group(gazeTr("连接"), fTest));

    return wrapTitled(gazeTr("以文搜图"), root);
}
