#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTreeWidget>
#include <QTimer>
#include <QQueue>
#include <QRegularExpression>
#include <QStringList>

class QTreeWidgetItem;

// 名称搜索对话框:文件夹树右键"搜索..."。在指定根目录内按名称匹配文件/文件夹。
// 扫描由定时器按"每拍 6ms 预算"驱动:大目录树下界面不冻结、结果边扫边出;
// 深度/目录数/命中数都有硬上限,触顶时状态栏如实写明,不假装扫完。
class SearchDialog : public QDialog {
    Q_OBJECT
public:
    explicit SearchDialog(const QString& rootDir, QWidget* parent = nullptr);

protected:
    void closeEvent(QCloseEvent* ev) override;

private:
    void startSearch();
    void stepScan();
    void stopScan(const QString& tail);
    void openResult(QTreeWidgetItem* it);
    bool matches(const QString& name) const;

    QLineEdit*   m_include;
    QLineEdit*   m_exclude;
    QCheckBox*   m_recurse;
    QCheckBox*   m_hidden;
    QCheckBox*   m_dirsToo;
    QPushButton* m_runBtn;
    QPushButton* m_stopBtn;
    QLabel*      m_status;
    QTreeWidget* m_results;
    QTimer       m_ticker;

    QString m_root;
    QQueue<QPair<QString, int>> m_queue;   // 待扫目录 + 深度
    QStringList m_incPlain;                // 子串词:命中任一即算
    QStringList m_excPlain;
    QList<QRegularExpression> m_incWild;   // 通配词(* ?)
    QList<QRegularExpression> m_excWild;
    bool m_skipHidden = true;
    bool m_wantDirs = false;
    bool m_running = false;
    int  m_scannedDirs = 0;
    int  m_matches = 0;
};
