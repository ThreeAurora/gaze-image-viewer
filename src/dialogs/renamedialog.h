#pragma once
// ═══════════════════════════════════════════════════════
// 文件/文件夹重命名对话框(2026-09-02 用户令:太简陋,至少像 XnView 那样)
//
// 形态:标题「文件重命名」,输入框右侧/下方提供「插入日期/时间»」,
//       放入当前光标处以 yyyy-MM-dd_HH-mm-ss 直接拼进文件名。
// 文件页(mainwindow_ops.cpp renameCurrent)与文件树(foldertree.cpp renameItem)
// 共用本对话框,避免两份 QInputDialog 各自简陋又不同步。
// ═══════════════════════════════════════════════════════
#include <QDialog>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <QDateTime>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QKeyEvent>

class RenameDialog : public QDialog {
    Q_OBJECT
public:
    // oldName:当前文件名(含扩展名),默认全选便于整条替换。
    // 与 QInputDialog::getText 同语义:用户取消了打回空串。
    static QString getName(QWidget* parent, const QString& oldName) {
        RenameDialog dlg(parent, oldName);
        return dlg.exec() == QDialog::Accepted ? dlg.resultName().trimmed()
                                               : QString();
    }

private:
    explicit RenameDialog(QWidget* parent, const QString& oldName)
        : QDialog(parent) {
        setWindowTitle(QString::fromUtf8("文件重命名"));
        setMinimumWidth(420);

        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(16, 14, 16, 14);
        root->setSpacing(10);

        auto* label = new QLabel(QString::fromUtf8("新文件名:"), this);

        // 输入行:输入框 + 插入日期/时间(N 版主视觉在右侧,给足换行的余量)
        auto* row = new QHBoxLayout;
        row->setSpacing(8);
        m_edit = new QLineEdit(oldName, this);
        m_edit->selectAll();
        m_edit->setMinimumWidth(240);
        row->addWidget(m_edit, 1);

        auto* insertBtn = new QPushButton(QString::fromUtf8("插入日期/时间\xc2\xbb"), this);
        insertBtn->setCursor(Qt::PointingHandCursor);
        insertBtn->setFlat(true);
        insertBtn->setStyleSheet(QString::fromUtf8(
            "QPushButton{color:#4C9AF5;background:transparent;border:none;padding:4px 8px;}"
            "QPushButton:hover{color:#6FB1FF;text-decoration:underline;}"));
        row->addWidget(insertBtn);
        connect(insertBtn, &QPushButton::clicked, this, &RenameDialog::insertDateTime);

        root->addWidget(label);
        root->addLayout(row);

        // 按钮行:确定(默认)+ 取消
        auto* btns = new QHBoxLayout;
        btns->addStretch(1);
        auto* okBtn = new QPushButton(QString::fromUtf8("确定"), this);
        auto* cancelBtn = new QPushButton(QString::fromUtf8("取消"), this);
        okBtn->setDefault(true);
        btns->addWidget(okBtn);
        btns->addWidget(cancelBtn);
        root->addLayout(btns);

        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        m_edit->setFocus();
    }

    QString resultName() const { return m_edit->text(); }

    // 把当前时间戳插入到光标处(如 IMG_123.jpg → IMG_2026-09-02_15-30-45_123.jpg)
    void insertDateTime() {
        const QString stamp =
            QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
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
};