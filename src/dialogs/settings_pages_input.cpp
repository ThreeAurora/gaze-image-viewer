#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QProcess>
#include <QMessageBox>
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
#include "settings_dialog_internal.h"

using namespace sd_impl;   // colorPick

QWidget* SettingsDialog::pageKeyboardMouse() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(QString::fromUtf8("左/右键方向键"),
        combo("Keyboard/leftRight", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("水平滚动")}, 0));
    form->addRow(QString::fromUtf8("上/下方向键"),
        combo("Keyboard/upDown", {QString::fromUtf8("上一个文件/下一个文件"),
            QString::fromUtf8("向上/向下翻页")}, 0));
    form->addRow(QString::fromUtf8("空格"),
        combo("Keyboard/space", {QString::fromUtf8("播放/暂停(视频)"),
            QString::fromUtf8("什么都不做"),
            QString::fromUtf8("下一个文件"), QString::fromUtf8("快速幻灯片")}, 0));
    form->addRow(QString::fromUtf8("快速幻灯片间隔(毫秒)"),
        spin("Interface/slideInterval", SLIDE_MS_MIN, SLIDE_MS_MAX, SLIDE_MS_DEF));
    // Viewer/seekSeconds:Ctrl+PgUp/PgDn 一次跳多少秒(1-3600)。
    // 来源 @147575「右键+滚轮具体滚动多少秒…应该在设置里能体现,从1秒到3600秒」;
    // 右键+滚轮后来被用户改判为"等同于 Ctrl+滚轮缩放"(@635777),秒数落到快进快退上
    form->addRow(QString::fromUtf8("快进/快退秒数"),
        spin("Viewer/seekSeconds", 1, 3600, 3));
    form->addRow(chk("Keyboard/escCloseBrowser", QString::fromUtf8("按 ESC 关闭:浏览器模式"), false));
    form->addRow(chk("Keyboard/escCloseViewer", QString::fromUtf8("按 ESC 关闭:查看器"), true));
    return wrapTitled(QString::fromUtf8("键盘"), form);
}

QWidget* SettingsDialog::pageShortcuts() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);

    // ── 查看器命令表(与 previewpanel 共用 viewerhotkeys.h,不再两处维护) ──

    // 顶部行:模式切换 + 筛选
    auto* top = new QHBoxLayout;
    auto* modeCombo = new QComboBox;
    modeCombo->addItems({QString::fromUtf8("浏览器模式"), QString::fromUtf8("查看器")});
    top->addWidget(modeCombo);
    auto* filterEdit = new QLineEdit;
    filterEdit->setPlaceholderText(QString::fromUtf8("筛选"));
    filterEdit->setClearButtonEnabled(true);
    top->addWidget(filterEdit, 1);
    root->addLayout(top);

    // 三列表格:动作 | 命令名 | 快捷键(行内编辑)
    auto* table = new QTableWidget(0, 3);
    table->setHorizontalHeaderLabels({QString::fromUtf8("动作"),
                                      QString::fromUtf8("命令名"),
                                      QString::fromUtf8("快捷键")});
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->verticalHeader()->setVisible(false);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setStyleSheet(
        QString::fromUtf8("QTableWidget{background:%1;color:%2;border:1px solid %3;}"
        "QHeaderView::section{background:%1;color:%2;"
        "border:none;padding:4px;}").arg(C_CONTENT, C_TEXT, C_SEPARATOR));
    table->setColumnWidth(1, 190);
    root->addWidget(table, 1);

    // 底部:选中命令的快捷方式(无/默认/自定义)
    auto* footRow = new QHBoxLayout;
    auto* rbNone   = new QRadioButton(QString::fromUtf8("无"));
    auto* rbDef    = new QRadioButton(QString::fromUtf8("默认"));
    auto* rbCustom = new QRadioButton(QString::fromUtf8("自定义"));
    auto* capEdit  = new QKeySequenceEdit;
    capEdit->setEnabled(false);
    footRow->addWidget(rbNone);
    footRow->addWidget(rbDef);
    footRow->addWidget(rbCustom);
    footRow->addWidget(capEdit, 1);
    root->addWidget(group(QString::fromUtf8("选中命令的快捷方式"), footRow));

    // 固定鼠标绑定说明(2026-08-30 裁决:鼠标设置页已删,绑定写死在代码里)
    auto* mouseTip = new QLabel(QString::fromUtf8(
        "鼠标：滚轮 = 上一个/下一个文件；Ctrl+滚轮 或 按住右键+滚轮 = 放大/缩小；"
        "左键拖动 = 移动画面；右键 = 上下文菜单；中键动作见「切换模式」页。"));
    mouseTip->setWordWrap(true);
    mouseTip->setStyleSheet(
        QStringLiteral("color:#B8B8C0;font-size:12px;background:transparent;"));
    root->addWidget(mouseTip);

    // ── 数据填充 ──
    // 查看器快捷键改动需刷新 PreviewPanel 的快捷键缓存(经元调用,免头文件依赖)
    auto notifyPreview = [this]() {
        if (QWidget* mw = parentWidget())
            for (QWidget* w : mw->findChildren<QWidget*>())
                if (w->metaObject()->className() == QByteArrayLiteral("PreviewPanel"))
                    QMetaObject::invokeMethod(w, "reloadViewerHotkeys");
    };

    auto fillTable = [this, table, modeCombo, filterEdit, notifyPreview]() {
        table->blockSignals(true);
        table->setRowCount(0);
        const bool viewerMode = modeCombo->currentIndex() == 1;
        const QString f = filterEdit->text().trimmed();
        if (!viewerMode) {
            // 浏览器模式:主窗口全部带快捷键的菜单动作(原 shortcut 存为"默认键")
            if (QWidget* mw = parentWidget()) {
                QList<QAction*> acts = mw->findChildren<QAction*>();
                std::sort(acts.begin(), acts.end(),
                          [](QAction* a, QAction* b) { return a->text() < b->text(); });
                for (QAction* a : acts) {
                    if (a->shortcut().isEmpty() || a->text().isEmpty()
                        || a->menu() != nullptr || a->isSeparator())
                        continue;
                    QString name = a->text();
                    name.remove('&');
                    if (!f.isEmpty() && !name.contains(f, Qt::CaseInsensitive)) continue;
                    int r = table->rowCount();
                    table->insertRow(r);
                    table->setItem(r, 0, new QTableWidgetItem(name));
                    table->setItem(r, 1, new QTableWidgetItem("Shortcuts/" + name));
                    table->item(r, 0)->setData(Qt::UserRole, a->shortcut().toString());
                    auto* ed = new QKeySequenceEdit(a->shortcut());
                    QString key = QString("Shortcuts/") + name;
                    connect(ed, &QKeySequenceEdit::keySequenceChanged, this,
                            [a, key](const QKeySequence& ks) {
                                AppSettings::instance().set(key, ks.toString());
                                a->setShortcut(ks);
                            });
                    table->setCellWidget(r, 2, ed);
                }
            }
        } else {
            // 查看器:固定动作表(ini ViewerShortcut/*)
            for (const auto& c : viewerHotkeyCmds()) {
                QString name = QString::fromUtf8(c.name);
                if (!f.isEmpty() && !name.contains(f, Qt::CaseInsensitive)) continue;
                int r = table->rowCount();
                table->insertRow(r);
                table->setItem(r, 0, new QTableWidgetItem(name));
                table->setItem(r, 1, new QTableWidgetItem("ViewerShortcut/" + name));
                table->item(r, 0)->setData(Qt::UserRole, QString::fromLatin1(c.defKey));
                QString key = QStringLiteral("ViewerShortcut/") + name;
                QString cur = AppSettings::instance().get(key, QString::fromLatin1(c.defKey)).toString();
                auto* ed = new QKeySequenceEdit(QKeySequence(cur));
                connect(ed, &QKeySequenceEdit::keySequenceChanged, this,
                        [this, key, notifyPreview](const QKeySequence& ks) {
                            AppSettings::instance().set(key, ks.toString());
                            notifyPreview();
                        });
                table->setCellWidget(r, 2, ed);
            }
        }
        table->blockSignals(false);
    };

    connect(modeCombo, &QComboBox::currentIndexChanged, this, fillTable);
    connect(filterEdit, &QLineEdit::textChanged, this, fillTable);
    fillTable();

    // 行选中 → 底部单选组反映当前状态
    connect(table, &QTableWidget::itemSelectionChanged, this, [this, table, rbNone, rbDef, rbCustom, capEdit]() {
        int r = table->currentRow();
        auto* ed = (r >= 0) ? qobject_cast<QKeySequenceEdit*>(table->cellWidget(r, 2)) : nullptr;
        rbNone->setChecked(false); rbDef->setChecked(false); rbCustom->setChecked(false);
        capEdit->setEnabled(false);
        if (!ed) return;
        QString iniKey = table->item(r, 1)->text();
        // 默认值:浏览器=空串(读回原),查看器=读默认表;简化:值等于 ini 存的
        QKeySequence cur = ed->keySequence();
        if (cur.isEmpty()) rbNone->setChecked(true);
        else { rbCustom->setChecked(true); capEdit->setEnabled(true); capEdit->setKeySequence(cur); }
        Q_UNUSED(iniKey);
    });

    // 单选切换 → 应用到选中行
    auto applyMode = [this, table, rbNone, rbDef, rbCustom, capEdit, notifyPreview]() {
        int r = table->currentRow();
        if (r < 0) return;
        auto* ed = qobject_cast<QKeySequenceEdit*>(table->cellWidget(r, 2));
        if (!ed) return;
        QString iniKey = table->item(r, 1)->text();
        QString defVal = table->item(r, 0)->data(Qt::UserRole).toString();
        QAction* act = nullptr;
        if (iniKey.startsWith("Shortcuts/") && parentWidget()) {
            QString name = iniKey.mid(int(QString("Shortcuts/").size()));
            for (QAction* a : parentWidget()->findChildren<QAction*>())
                if (a->text().remove('&') == name) { act = a; break; }
        }
        const bool isViewer = iniKey.startsWith("ViewerShortcut/");
        if (rbNone->isChecked()) {
            ed->setKeySequence(QKeySequence());
            AppSettings::instance().set(iniKey, QString());
            if (act) act->setShortcut(QKeySequence());
            if (isViewer) notifyPreview();
            capEdit->setEnabled(false); capEdit->clear();
        } else if (rbDef->isChecked()) {
            ed->setKeySequence(QKeySequence(defVal));
            AppSettings::instance().set(iniKey, defVal);
            if (act) act->setShortcut(QKeySequence(defVal));
            if (isViewer) notifyPreview();
            capEdit->setEnabled(false); capEdit->clear();
        } else if (rbCustom->isChecked()) {
            capEdit->setEnabled(true);
            capEdit->setKeySequence(ed->keySequence());
        }
    };
    connect(rbNone, &QRadioButton::toggled, this, applyMode);
    connect(rbDef,  &QRadioButton::toggled, this, applyMode);
    connect(rbCustom, &QRadioButton::toggled, this, applyMode);
    connect(capEdit, &QKeySequenceEdit::keySequenceChanged, this,
            [table](const QKeySequence& ks) {
                int r = table->currentRow();
                auto* ed = (r >= 0) ? qobject_cast<QKeySequenceEdit*>(table->cellWidget(r, 2)) : nullptr;
                if (ed && ed->keySequence() != ks) ed->setKeySequence(ks);
            });

    return wrapTitled(QString::fromUtf8("快捷键"), root);
}

QWidget* SettingsDialog::pageBrowser() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距);#148 再收一档

    // 分组"预览"(对齐 XnView MP 浏览器页)
    auto* fPrev = new QFormLayout;
    fPrev->setVerticalSpacing(6);
    fPrev->addRow(QString::fromUtf8("预览背景色"),
                  colorPick("Browser/previewBackColor", "#000000"));
    fPrev->addRow(chk("Browser/showRating", QString::fromUtf8("显示颜色标记"), true));
    // #111(用户 2026-08-31):文本/PDF 预览单独成开关且默认关,打勾才预览。
    // 长文本另按"行数 + 每行字符数"截断,上限与实测依据见 textlimit.h。
    fPrev->addRow(chk("Preview/previewTxt",
                      QString::fromUtf8("预览 txt 文本文件内容(超长自动截断)"), false));
    fPrev->addRow(chk("Preview/showMd",
                      QString::fromUtf8("以 MD 格式预览 Markdown 文件(超长自动截断)"), false));
    // PDF 用随 gaze 分发的内置 Ghostscript(gs/),不再要求系统安装(#110)
    fPrev->addRow(chk("Preview/showPdf", QString::fromUtf8("预览 PDF 文档(内置 Ghostscript 渲染)"), false));
    root->addWidget(group(QString::fromUtf8("预览"), fPrev));

    // 分组"旋转"
    auto* fRot = new QFormLayout;
    fRot->setVerticalSpacing(6);
    fRot->addRow(chk("Browser/rotateExifOnly", QString::fromUtf8("仅改变 EXIF 方向(如果可能)"), true));
    fRot->addRow(chk("Browser/losslessRotate", QString::fromUtf8("使用无损旋转(如果可能)"), true));
    root->addWidget(group(QString::fromUtf8("旋转"), fRot));

    auto* fMisc = new QFormLayout;
    fMisc->setVerticalSpacing(6);
    fMisc->addRow(chk("Browser/thumbScrollPreview", QString::fromUtf8("用缩略图查看滚动内容"), true));
    fMisc->addRow(chk("Browser/showDesktopInTree", QString::fromUtf8("在文件夹树中显示\"桌面\""), true));
    // #117:文件树左键按住拖动的语义。默认=扫过即切入,方便连续快速预览不同目录
    fMisc->addRow(QString::fromUtf8("文件树左键按住拖动"),
        combo("FolderTree/leftDragSweep", {QString::fromUtf8("切换文件夹(扫过即进入)"),
                                           QString::fromUtf8("拖动多选(原行为)")}, 0));
    root->addLayout(fMisc);
    return wrapTitled(QString::fromUtf8("浏览器"), root);
}

QWidget* SettingsDialog::pageFileList() {
    auto* form = new QFormLayout;
    form->setVerticalSpacing(6);
    form->addRow(chk("FileList/showHidden", QString::fromUtf8("显示隐藏的文件和文件夹"), true));
    form->addRow(chk("FileList/recognizeByExt", QString::fromUtf8("只按扩展名进行识别文件格式"), true));
    form->addRow(QString::fromUtf8("扫描文件头"),
        combo("FileList/scanHeader", {QString::fromUtf8("总是"),
            QString::fromUtf8("排除软盘/CD/DVD"), QString::fromUtf8("仅电脑本地硬盘"),
            QString::fromUtf8("从不")}, 0));
    form->addRow(chk("FileList/mixSort", QString::fromUtf8("混合文件/文件夹排序"), false));
    form->addRow(chk("FileList/folderAlphabetical", QString::fromUtf8("文件夹总是按字母序排列"), true));
    // #150:启动默认排序,索引含义与 FileGrid 构造函数里的 switch 一一对应
    form->addRow(QString::fromUtf8("启动时默认排序"),
        combo("Browser/startupSort", {QString::fromUtf8("文件名(升序)"),
            QString::fromUtf8("修改日期(降序)"), QString::fromUtf8("创建日期(降序)"),
            QString::fromUtf8("EXIF 拍摄日期(降序)"), QString::fromUtf8("类型"),
            QString::fromUtf8("大小(降序)"), QString::fromUtf8("扩展名"),
            QString::fromUtf8("路径"), QString::fromUtf8("颜色标签"),
            QString::fromUtf8("记住上次")}, 0));
    // #150:启动默认排序,索引含义与 FileGrid 构造函数里的 switch 一一对应
    form->addRow(QString::fromUtf8("启动时默认排序"),
        combo("Browser/startupSort", {QString::fromUtf8("文件名(升序)"),
            QString::fromUtf8("修改日期(降序)"), QString::fromUtf8("创建日期(降序)"),
            QString::fromUtf8("EXIF 拍摄日期(降序)"), QString::fromUtf8("类型"),
            QString::fromUtf8("大小(降序)"), QString::fromUtf8("扩展名"),
            QString::fromUtf8("路径"), QString::fromUtf8("颜色标签"),
            QString::fromUtf8("记住上次")}, 0));
    form->addRow(chk("FileList/newAtEnd", QString::fromUtf8("新文件添加至列表末尾"), false));
    form->addRow(chk("FileList/autoSelectNew", QString::fromUtf8("自动选择新文件"), false));
    form->addRow(chk("FileList/sizeInBytes", QString::fromUtf8("按字节显示文件大小"), false));
    return wrapTitled(QString::fromUtf8("文件列表"), form);
}
