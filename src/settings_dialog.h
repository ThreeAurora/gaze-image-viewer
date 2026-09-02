#pragma once
#include <QDialog>
#include <QTreeWidget>
#include <QStackedWidget>
#include <QCheckBox>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QFormLayout>

class QGroupBox;

// 设置对话框:左侧一级/二级分类树(一级项自身也是页面,可点进)
// + 右侧大标题/分隔线/分组框页面(对标 XnView MP 结构)
// 变更即时保存到 gaze.ini
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget* parent = nullptr);

private:
    QWidget* pageGeneral();        // 常规
    QWidget* pageStartup();        // 常规 → 启动
    QWidget* pageFileOps();        // 常规 → 文件操作
    QWidget* pageInterface();      // 常规 → 界面(标签卡/最近文件)
    QWidget* pageTitlebar();       // 常规 → 标题栏(模板+变量插入菜单)
    QWidget* pageKeyboardMouse();  // 交互 → 键盘和鼠标(键盘部分)
    QWidget* pageShortcuts();      // 交互 → 快捷键配置(+固定鼠标绑定说明)
    QWidget* pageSwitchMode();     // 常规 → 切换模式
    QWidget* pageBrowser();        // 浏览器
    QWidget* pageFileList();       // 浏览器 → 文件列表
    QWidget* pageThumbs();         // 缩略图
    QWidget* pageAppearance();     // 缩略图 → 外观
    QWidget* pageLabelColors();    // 缩略图 → 标签颜色(扩展名底色列表编辑)
    QWidget* pageViewer();         // 查看
    QWidget* pageViewerOther();    // 查看 → 其他(播放与性能)
    QWidget* pageFullscreen();     // 查看 → 全屏
    QWidget* pageCache();          // 高级 → 缓存数据库
    QWidget* pageMaintenance();    // 维护(缩略图库统计/筛选/删除/重建)
    QWidget* pageIntegration();    // 高级 → 系统集成
    QWidget* pageImgSearch();      // 以文搜图(万象图搜服务位置/生命周期/测试连接)

    void populatePages();          // 构建/重建全部分类与页面(恢复默认后调用)

    QTreeWidget*    m_cats;
    QStackedWidget* m_stack;
    // #126:「缩略图 → 标签颜色」(真正的颜色编辑器) 那一页的树节点,
    // 外观页的"打开颜色编辑器"按钮靠它跳转
    QTreeWidgetItem* m_labelColorsItem = nullptr;

    // 控件工厂:载入当前值,变更即时保存
    QCheckBox* chk(const QString& key, const QString& label, bool def);
    QComboBox* combo(const QString& key, const QStringList& items, int defIdx);
    QSpinBox*  spin(const QString& key, int min, int max, int def);
    QLineEdit* edit(const QString& key, const QString& def);
};

// ── 页面构建助手(pageXXX 与 settings_dialog.cpp 内部共用) ──
// 带大标题+分隔线的页面包装
QWidget* wrapTitled(const QString& title, QLayout* lay);
// 分组框(组名在框外上方 + 浅色细线圆角框,XnView 式)
QWidget* group(const QString& title, QLayout* lay);
