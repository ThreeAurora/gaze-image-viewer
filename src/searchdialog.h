#pragma once
#include <QDialog>
#include <QLineEdit>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QTreeWidget>
#include <QTimer>
#include <QStringList>

class QTreeWidgetItem;

// 名称搜索对话框:文件夹树右键"搜索..."。在指定根目录内按名称匹配文件。
// 扫描按"每拍一个时间预算"由定时器驱动:大目录树下界面不冻结,结果边扫边出,
// 上限写进状态栏,不假装扫完了。
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
    QLabel*      m_status;
    QTreeWidget* m_results;

    QString m_root;
    QStringList m_queue;        // 待扫目录(BFS)
    QStringList m_incTerms;     // 命中任一即算
    QStringList m_excTerms;
    bool m_running = false;
    int  m_scannedDirs = 0;
    int  m_matches = 0;
};
