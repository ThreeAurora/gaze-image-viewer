#pragma once
#include <QDialog>
#include <QListWidget>
#include <QStackedWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>

// 设置对话框:左侧分类 + 右侧页(照 XnView MP 结构;默认值 = 用户 ini 配置)
// 变更即时保存到 xnnview.ini
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QWidget* pageGeneral();        // 常规
    QWidget* pageStartup();        // 启动
    QWidget* pageFileOps();        // 文件操作
    QWidget* pageInterface();      // 界面(标题栏模板/最近文件)
    QWidget* pageKeyboardMouse();  // 键盘和鼠标
    QWidget* pageShortcuts();      // 快捷键配置
    QWidget* pageSwitchMode();     // 切换模式
    QWidget* pageBrowser();        // 浏览器
    QWidget* pageFileList();       // 文件列表
    QWidget* pageThumbs();         // 缩略图
    QWidget* pageAppearance();     // 外观(标签颜色)
    QWidget* pageViewer();         // 查看
    QWidget* pageFullscreen();     // 全屏
    QWidget* pageCache();          // 分类(缓存数据库)
    QWidget* pageIntegration();    // 系统集成

    void populatePages();          // 构建/重建全部分类与页面(恢复默认后调用)

    QListWidget*    m_cats;
    QStackedWidget* m_stack;

    // 控件工厂:载入当前值,变更即时保存
    QCheckBox* chk(const QString& key, const QString& label, bool def);
    QComboBox* combo(const QString& key, const QStringList& items, int defIdx);
    QSpinBox*  spin(const QString& key, int min, int max, int def);
    QLineEdit* edit(const QString& key, const QString& def);
};
