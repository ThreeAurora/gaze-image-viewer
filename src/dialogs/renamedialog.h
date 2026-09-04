#pragma once
// ═══════════════════════════════════════════════════════
// 文件/文件夹重命名对话框(2026-09-02 用户令:太简陋,至少像 XnView 那样;
// 2026-09-04 #244 两细节:日期格式下拉可选 + 扩展名框仅文件重命名时有)
//
// 形态:标题「文件重命名」,一行内 = 主名输入框 + 「插入日期/时间»」
//       (MenuButtonPopup:点主体用上次格式插入,点箭头弹 XnView 八种格式)
//       + 「扩展名:」小框(仅文件;文件夹名可含点,绝不拆)。
//       结果名 = 主名 + "." + 扩展名(扩展名空则不加点的原样主名)。
// 文件页(mainwindow_ops.cpp renameCurrent)与文件树(foldertree.cpp renameItem)
// 共用本对话框,避免两份 QInputDialog 各自简陋又不同步。
// ═══════════════════════════════════════════════════════
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QToolButton>
#include <QMenu>
#include <QLabel>
#include <QDateTime>
#include <QVector>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include "i18n.h"

class RenameDialog : public QDialog {
    Q_OBJECT
public:
    // oldName:当前文件名(含扩展名),默认全选主名便于整条替换。
    // isDir=true:文件夹重命名——不拆扩展名、不显示扩展名框(#244)。
    // 与 QInputDialog::getText 同语义:用户取消了打回空串。
    static QString getName(QWidget* parent, const QString& oldName,
                           bool isDir = false) {
        RenameDialog dlg(parent, oldName, isDir);
        return dlg.exec() == QDialog::Accepted ? dlg.resultName().trimmed()
                                               : QString();
    }

private:
    // #244:XnView 的八种日期格式(token 本身即菜单文案,属设计内不翻)
    static const QVector<QString>& dateFormats() {
        static const QVector<QString> f = {
            QStringLiteral("hh-mm-ss"), QStringLiteral("yyyy-MM-dd-hh-mm-ss"),
            QStringLiteral("yyyy-MM-dd-hh-mm"), QStringLiteral("yyyy-MM-dd-hh"),
            QStringLiteral("yyyy"), QStringLiteral("yyyy-MM"),
            QStringLiteral("yyyy-MM-dd"), QStringLiteral("MM-dd")};
        return f;
    }

    explicit RenameDialog(QWidget* parent, const QString& oldName, bool isDir)
        : QDialog(parent) {
        setWindowTitle(gazeTr("文件重命名"));
        setMinimumWidth(460);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 14, 16, 14);
        root->setSpacing(10);

        // 文件才拆主名/扩展名(#244);文件夹名可含点,整体进主名框
        QString base = oldName;
        QString ext;
        if (!isDir) {
            const int dot = oldName.lastIndexOf(QLatin1Char('.'));
            if (dot > 0) {   // dot==0 → ".git" 这类隐藏名不拆
                base = oldName.left(dot);
                ext = oldName.mid(dot + 1);
            }
        }

        auto* label = new QLabel(gazeTr("新文件名:"), this);

        // 输入行:主名框 + 插入日期/时间(带格式下拉) + 扩展名框(仅文件)
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        m_edit = new QLineEdit(base, this);
        m_edit->selectAll();
        m_edit->setMinimumWidth(200);
        row->addWidget(m_edit, 1);

        auto* insertBtn = new QToolButton(this);
        insertBtn->setText(gazeTr("插入日期/时间»"));
        insertBtn->setCursor(Qt::PointingHandCursor);
        insertBtn->setStyleSheet(QString::fromUtf8(
            "QToolButton{color:#4C9AF5;background:transparent;border:none;"
            "padding:4px 8px;}"
            "QToolButton:hover{color:#6FB1FF;text-decoration:underline;}"));
        auto* fmtMenu = new QMenu(insertBtn);
        for (const QString& f : dateFormats()) {
            QAction* a = fmtMenu->addAction(f);
            connect(a, &QAction::triggered, this, [this, f] {
                m_lastFmt = f;
                insertDateTime(f);
            });
        }
        insertBtn->setMenu(fmtMenu);
        // MenuButtonPopup:点主体=按上次所选格式插入,点箭头=选格式
        insertBtn->setPopupMode(QToolButton::MenuButtonPopup);
        connect(insertBtn, &QToolButton::clicked,
                this, [this] { insertDateTime(m_lastFmt); });
        row->addWidget(insertBtn);

        if (!isDir) {
            auto* extLabel = new QLabel(gazeTr("扩展名:"), this);
            row->addWidget(extLabel);
            m_extEdit = new QLineEdit(ext, this);
            m_extEdit->setFixedWidth(72);
            m_extEdit->setPlaceholderText(QStringLiteral("jpg"));
            row->addWidget(m_extEdit);
        }

        root->addWidget(label);
        root->addLayout(row);

        // 按钮行:确定(默认)+ 取消
        auto* btns = new QHBoxLayout;
        btns->addStretch(1);
        auto* okBtn = new QPushButton(gazeTr("确定"), this);
        auto* cancelBtn = new QPushButton(gazeTr("取消"), this);
        okBtn->setDefault(true);
        btns->addWidget(okBtn);
        btns->addWidget(cancelBtn);
        root->addLayout(btns);

        connect(okBtn, &QPushButton::clicked, this, [this] {
            if (m_edit->text().trimmed().isEmpty()) return;   // 主名空不放行
            accept();
        });
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        m_edit->setFocus();
    }

    QString resultName() const {
        const QString base = m_edit->text().trimmed();
        if (!m_extEdit) return base;
        const QString ext = m_extEdit->text().trimmed();
        return ext.isEmpty() ? base : base + QLatin1Char('.') + ext;
    }

    // 把当前时间戳按指定格式插入到主名框光标处
    void insertDateTime(const QString& fmt) {
        const QString stamp = QDateTime::currentDateTime().toString(fmt);
        const int c = m_edit->cursorPosition();
        QString t = m_edit->text();
        t.insert(c, stamp);
        m_edit->setText(t);
        m_edit->setCursorPosition(c + stamp.size());
        m_edit->setFocus();
    }

    void keyPressEvent(QKeyEvent* e) override {
        // Enter 确认、Esc 取消,与对话框默认行为一致(不额外拦)
        QDialog::keyPressEvent(e);
    }

    QLineEdit* m_edit = nullptr;
    QLineEdit* m_extEdit = nullptr;   // 仅文件重命名时非空(#244)
    QString m_lastFmt = QStringLiteral("yyyy-MM-dd-hh-mm-ss");
};
