#include "settings_dialog.h"
#include "settings.h"
#include "integration.h"
#include "labelstore.h"
#include "constants.h"
#include "dbprefix.h"
#include "viewerhotkeys.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QGridLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QPushButton>
#include <QLabel>
#include <QApplication>
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

QWidget* SettingsDialog::pageThumbs() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);   // 分组框页:组间距收紧(组框自身已有边距)

    // 分组"创建"
    auto* fCreate = new QFormLayout;
    fCreate->setVerticalSpacing(6);
    fCreate->addRow(chk("Thumbs/folder4", QString::fromUtf8("在文件夹缩略图中显示4张缩略图(而非1张)"), true));
    fCreate->addRow(chk("Thumbs/video4", QString::fromUtf8("在视频缩略图中显示4张缩略图(替代1张)"), false));
    fCreate->addRow(chk("Thumbs/highQuality", QString::fromUtf8("创建高品质的缩略图"), true));
    fCreate->addRow(chk("Thumbs/useEmbedded", QString::fromUtf8("使用嵌入缩略图"), true));
    fCreate->addRow(chk("Thumbs/embedFallback", QString::fromUtf8("当内嵌缩略图尺寸小于缩略图尺寸时从原图创建"), true));
    fCreate->addRow(chk("Thumbs/wholeFolder", QString::fromUtf8("为整个文件夹创建缩略图"), false));
    fCreate->addRow(QString::fromUtf8("视频提取帧位置(%,0=第 1 秒)"),
        spin("Thumbs/videoFramePct", 0, 100, 0));
    root->addWidget(group(QString::fromUtf8("创建"), fCreate));

    // 分组"处理"
    auto* fProc = new QFormLayout;
    fProc->setVerticalSpacing(6);
    fProc->addRow(chk("Thumbs/alpha", QString::fromUtf8("使用alpha通道"), true));
    fProc->addRow(chk("Thumbs/transparencyGrid", QString::fromUtf8("使用透明网格"), true));
    fProc->addRow(chk("Thumbs/sharpen", QString::fromUtf8("锐化缩略图"), false));
    fProc->addRow(chk("Thumbs/gamma", QString::fromUtf8("使用 Gamma 纠正"), false));
    root->addWidget(group(QString::fromUtf8("处理"), fProc));
    return wrapTitled(QString::fromUtf8("缩略图"), root);
}

QWidget* SettingsDialog::pageAppearance() {
    auto* form = new QFormLayout;
    // 主题(立即生效,2026-09-02 用户令):切换时重读 Theme::init + 全局重刷 QSS。
    // C_* 宏在 paintEvent 里也是 T() 双档即时求值,所以换主题只需重设样式表
    // 并让各控件重绘 —— 不再需要重启。
    auto* themeCombo = new QComboBox;
    themeCombo->addItems({QString::fromUtf8("深色"), QString::fromUtf8("浅色")});
    themeCombo->setCurrentIndex(
        AppSettings::instance().get("Appearance/theme", QStringLiteral("dark")).toString()
            == QLatin1String("light") ? 1 : 0);
    themeCombo->setToolTip(QString::fromUtf8("立即生效"));
    connect(themeCombo, &QComboBox::currentIndexChanged, this, [](int v) {
        AppSettings::instance().set("Appearance/theme", v == 1 ? QStringLiteral("light")
                                                               : QStringLiteral("dark"));
        Theme::init();                                   // 重读双档标志
        Theme::notifyChanged();                          // 重灌内联样式/缓存色(全量即时)
        if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance()))
            app->setStyleSheet(Theme::appQss());         // 全局样式表即时切换
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
        for (QWidget* w : QApplication::topLevelWidgets())
            w->update();                                 // 触发全窗口重绘刷新 T() 取色
#else
        Q_UNUSED(app)
#endif
    });
    form->addRow(QString::fromUtf8("主题(立即生效)"), themeCombo);
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 宽"),
        spin("Appearance/customThumbW", THUMB_W_MIN, THUMB_W_MAX, 96));
    // 0 = 与宽同高(接线前的既有行为);>0 才按设置值固定缩略图框高
    auto* thumbH = spin("Appearance/customThumbH", 0, 512, 0);
    thumbH->setSpecialValueText(QString::fromUtf8("跟随宽度"));
    form->addRow(QString::fromUtf8("自定义缩略图尺寸 - 高"), thumbH);
    form->addRow(chk("Appearance/shadow", QString::fromUtf8("使用阴影"), false));
    form->addRow(QString::fromUtf8("边框粗细"), spin("Appearance/borderSize", 0, 10, 0));
    form->addRow(QString::fromUtf8("间距"), spin("Appearance/spacing", 0, 40, 6));
    form->addRow(chk("Appearance/labelSpacing", QString::fromUtf8("标签间的间距"), true));
    form->addRow(QString::fromUtf8("图像对齐"),
        combo("Appearance/imageAlign", {QString::fromUtf8("左"), QString::fromUtf8("居中"),
            QString::fromUtf8("右")}, 1));
    form->addRow(QString::fromUtf8("标签排列"),
        combo("Appearance/labelAlign", {QString::fromUtf8("左"), QString::fromUtf8("居中"),
            QString::fromUtf8("右")}, 1));
    form->addRow(chk("Appearance/formatColor", QString::fromUtf8("文件根据格式显示以下颜色(文件名底色)"), true));
    // #126:这里原来是一段写死的 XnView 说明 + "(颜色编辑器即将支持)"。
    //   编辑器其实早就存在(「缩略图 → 标签颜色」:增删改扩展名、取色、写 ini、
    //   FileCard 真生效),那句占位话就是谎话。改成显示**当前真表**并一键跳过去。
    {
        QString t = QString::fromUtf8("标签颜色(扩展名 → 文件名底色,当前生效表):\n");
        QHash<QString, QStringList> byColor;
        for (const auto& e : LabelColors::all())
            byColor[e.second.name(QColor::HexRgb)].append(e.first);
        QStringList colors = byColor.keys();
        colors.sort();
        for (const QString& c : colors) {
            const QStringList exts = byColor.value(c);
            const QString shown = exts.size() > 8
                ? exts.mid(0, 8).join(',') + QString::fromUtf8(",…(共 %1 项)").arg(exts.size())
                : exts.join(',');
            t += QString::fromUtf8("  %1 ← %2\n").arg(c, shown);
        }
        t += QString::fromUtf8("未列出的格式:%1\n(上面总开关关掉时一律不上底色)")
                 .arg(LabelColors::fallbackColor().name(QColor::HexRgb));
        auto* lab = new QLabel(t);
        lab->setTextInteractionFlags(Qt::TextSelectableByMouse);
        form->addRow(lab);
    }
    auto* lcBtn = new QPushButton(
        QString::fromUtf8("打开颜色编辑器(缩略图 → 标签颜色)"));
    connect(lcBtn, &QPushButton::clicked, this, [this]() {
        if (m_labelColorsItem) m_cats->setCurrentItem(m_labelColorsItem);
    });
    form->addRow(lcBtn);
    return wrapTitled(QString::fromUtf8("外观"), form);
}

// ── 标签颜色页:扩展名列表整行底色填充,选中变蓝;右列输入/新建/移除/改色;底部默认色 ──
QWidget* SettingsDialog::pageLabelColors() {
    auto* root = new QVBoxLayout;
    root->setSpacing(6);

    root->addWidget(chk("Appearance/formatColor",
                        QString::fromUtf8("文件根据格式显示以下颜色(文件名底色)"), true));

    auto* body = new QHBoxLayout;
    body->setSpacing(8);

    // 左:扩展名列表(整行以对应颜色填充)
    auto* list = new QListWidget;
    list->setFixedWidth(320);
    list->setStyleSheet(
        QString::fromUtf8("QListWidget{background:%1;border:1px solid %2;outline:none;}"
        "QListWidget::item{height:24px;padding:0 8px;border:none;}"
        "QListWidget::item:selected{background:%3;color:%4;border:none;}").arg(C_CONTENT, C_SEPARATOR, C_ACCENT, C_TEXT));
    body->addWidget(list, 1);

    // 右:输入行 + 色块/按钮列
    auto* right = new QVBoxLayout;
    right->setSpacing(8);
    auto* inRow = new QHBoxLayout;
    inRow->addWidget(new QLabel(QString::fromUtf8("输入扩展名:")));
    auto* extEdit = new QLineEdit;
    extEdit->setPlaceholderText(QString::fromUtf8("gif"));
    extEdit->setFixedWidth(120);
    inRow->addWidget(extEdit);
    inRow->addSpacing(8);
    inRow->addWidget(new QLabel(QString::fromUtf8("或选一种格式:")));
    auto* extCombo = new QComboBox;
    {
        QStringList exts;
        for (const QString& e : IMAGE_EXTS)  exts << e.mid(1).toUpper();
        for (const QString& e : VIDEO_EXTS)  exts << e.mid(1).toUpper();
        for (const QString& e : AUDIO_EXTS)  exts << e.mid(1).toUpper();
        exts.removeDuplicates();
        std::sort(exts.begin(), exts.end());
        extCombo->addItems(exts);
    }
    inRow->addWidget(extCombo, 1);
    right->addLayout(inRow);

    // 色块行:左块=当前选中项颜色(点击改色) 右块=白色快选
    auto* swRow = new QHBoxLayout;
    swRow->setAlignment(Qt::AlignRight);
    auto* colorBtn = new QToolButton;
    colorBtn->setFixedSize(30, 24);
    auto* whiteBtn = new QToolButton;
    whiteBtn->setFixedSize(30, 24);
    whiteBtn->setStyleSheet(QString::fromUtf8("background:#FFFFFF;border:1px solid %1;").arg(C_SEPARATOR));
    whiteBtn->setToolTip(QString::fromUtf8("设为白色"));
    swRow->addWidget(colorBtn);
    swRow->addWidget(whiteBtn);
    right->addLayout(swRow);

    auto* addBtn = new QPushButton(QString::fromUtf8("新建"));
    auto* removeBtn = new QPushButton(QString::fromUtf8("移除"));
    right->addWidget(addBtn);
    right->addWidget(removeBtn);
    right->addStretch(1);

    body->addLayout(right);
    body->addStretch(1);
    root->addLayout(body, 1);

    // 底部:默认颜色(未列出格式的底色)
    auto* defRow = new QHBoxLayout;
    defRow->addWidget(new QLabel(QString::fromUtf8("默认颜色")));
    auto* defBtn = new QToolButton;
    defBtn->setFixedSize(30, 24);
    defBtn->setStyleSheet(QString("background:%1;border:1px solid %2;")
                              .arg(LabelColors::fallbackColor().name(), C_SEPARATOR));
    defRow->addWidget(defBtn);
    defRow->addStretch(1);
    root->addLayout(defRow);

    // ── 数据流 ──
    auto refill = [list, colorBtn]() {
        QListWidgetItem* sel = list->currentItem();
        QString selExt = sel ? sel->text() : QString();
        list->blockSignals(true);
        list->clear();
        const auto entries = LabelColors::all();
        for (const auto& pair : entries) {
            const QString& ext = pair.first;
            const QColor& col = pair.second;
            auto* it = new QListWidgetItem(ext);
            it->setBackground(col);
            it->setForeground(col.lightness() > 140 ? QColor("#000000") : QColor("#FFFFFF"));
            list->addItem(it);
            if (ext == selExt) list->setCurrentItem(it);
        }
        list->blockSignals(false);
        QColor cur = selExt.isEmpty() ? QColor("#191919")
                   : LabelColors::colorForExt(selExt);
        colorBtn->setStyleSheet(QString("background:%1;border:1px solid %2;")
                                    .arg(cur.name(), C_SEPARATOR));
    };
    auto syncColorBtn = [list, colorBtn]() {
        QListWidgetItem* it = list->currentItem();
        QColor cur = it ? LabelColors::colorForExt(it->text()) : QColor("#191919");
        colorBtn->setStyleSheet(QString("background:%1;border:1px solid %2;")
                                    .arg(cur.name(), C_SEPARATOR));
    };
    refill();

    connect(list, &QListWidget::itemSelectionChanged, this, syncColorBtn);
    connect(colorBtn, &QToolButton::clicked, this, [this, list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        QColor c = QColorDialog::getColor(LabelColors::colorForExt(it->text()),
                                          this, QString::fromUtf8("选择颜色"));
        if (!c.isValid()) return;
        LabelColors::set(it->text(), c);
        refill();
    });
    connect(whiteBtn, &QToolButton::clicked, this, [list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        LabelColors::set(it->text(), QColor("#FFFFFF"));
        refill();
    });
    connect(addBtn, &QPushButton::clicked, this, [extEdit, extCombo, list, refill]() {
        QString ext = extEdit->text().trimmed().toLower();
        if (ext.isEmpty()) ext = extCombo->currentText().trimmed().toLower();
        if (ext.isEmpty()) return;
        ext.remove(QRegularExpression("^[.*]+"));
        LabelColors::set(ext, LabelColors::fallbackColor());
        refill();
        for (int i = 0; i < list->count(); ++i)
            if (list->item(i)->text() == ext) { list->setCurrentRow(i); break; }
    });
    connect(removeBtn, &QPushButton::clicked, this, [list, refill]() {
        QListWidgetItem* it = list->currentItem();
        if (!it) return;
        LabelColors::remove(it->text());
        refill();
    });
    connect(defBtn, &QToolButton::clicked, this, [defBtn, refill]() {
        QColor c = QColorDialog::getColor(LabelColors::fallbackColor(),
                                          nullptr, QString::fromUtf8("默认颜色"));
        if (!c.isValid()) return;
        LabelColors::setFallbackColor(c);
        defBtn->setStyleSheet(QString("background:%1;border:1px solid %2;")
                                  .arg(c.name(), C_SEPARATOR));
        refill();
    });

    return wrapTitled(QString::fromUtf8("标签颜色"), root);
}
