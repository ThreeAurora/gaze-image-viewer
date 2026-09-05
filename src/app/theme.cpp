#include "theme.h"
#include "constants.h"
#include "settings.h"

#include <QApplication>
#include <QWidget>

namespace Theme {

namespace {
bool g_light = false;
std::vector<ChangeHandler> g_handlers;
}

void init() {
    g_light = AppSettings::instance().get("Appearance/theme", "dark").toString()
              == QLatin1String("light");
}

bool light() { return g_light; }

void addChangeHandler(ChangeHandler h) {
    g_handlers.push_back(std::move(h));
}

void notifyChanged() {
    for (const auto& h : g_handlers) h();
}

void applyLive() {
    init();                                   // 重读双档标志
    notifyChanged();                          // 重灌内联样式/缓存色(全量即时)
    if (QApplication* app = qobject_cast<QApplication*>(QApplication::instance()))
        app->setStyleSheet(appQss());         // 全局样式表即时切换
    for (QWidget* w : QApplication::topLevelWidgets())
        w->update();                          // 触发全窗口重绘刷新 T() 取色
}

QString appQss() {
    // 占位符按 %1..%25 顺序逐个 .arg:单个 arg() 每次替换最小编号占位符,
    // 取值全是 #hex,不含 %N 字样,链式安全
    return QStringLiteral(
        "QWidget {"
        "  font-family: \"Microsoft YaHei\", \"Segoe UI\", sans-serif;"
        "  font-size: 12px; color: %1; background: %2;"
        "}"
        "QMenu {"
        "  background: %3; color: %1;"
        "  border: 1px solid %4; border-radius: 6px; padding: 5px;"
        "}"
        "QMenu::item { padding: 6px 28px; border-radius: 4px; }"
        "QMenu::item:selected { background: %5; color: #FFF; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }"
        // ── 菜单勾选指示器(2026-09-04 用户令:先要"能看出可勾";随后 #246 再令:
        // 小一点、直角、更扁平)──原生 windowsvista 的对勾在深色菜单上几乎隐形;
        // 未勾=直角细描边空框(预告可勾),勾中=主题色纯色方块+白对勾,无描边无圆角
        // (扁平=没有任何立体/包边装饰)。image 用编译进 qrc 的 menu_check.png
        // (自有资产,不碰 assets/icons-48 的 XnView 图)。
        "QMenu::indicator { width: 13px; height: 13px; margin-left: 6px; }"
        "QMenu::indicator:unchecked {"
        "  border: 1px solid %11; border-radius: 0px; background: transparent;"
        "}"
        "QMenu::indicator:disabled { border: 1px solid %4; border-radius: 0px; }"
        "QMenu::indicator:checked {"
        "  background: %5; border: none;"
        "  image: url(:/menu_check.png);"
        "}"
        // ── 滚动条:轨道 + 滑块 + 两端箭头按钮(Windows/XnView 式) ──
        // margin 让出箭头按钮的位置,sub-line/add-line 用 subcontrol-origin:margin 落进去
        "QScrollBar:vertical {"
        "  background: %6; width: 15px; border: none;"
        "  margin: 16px 0px 16px 0px;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: %7; border-radius: 3px; min-height: 24px;"
        "  margin: 0px 2px 0px 2px;"
        "}"
        "QScrollBar::handle:vertical:hover { background: %8; }"
        "QScrollBar::add-line:vertical {"
        "  background: %9; border: none; height: 16px;"
        "  subcontrol-position: bottom; subcontrol-origin: margin;"
        "}"
        "QScrollBar::sub-line:vertical {"
        "  background: %9; border: none; height: 16px;"
        "  subcontrol-position: top; subcontrol-origin: margin;"
        "}"
        "QScrollBar::add-line:vertical:hover, QScrollBar::sub-line:vertical:hover {"
        "  background: %10;"
        "}"
        // 箭头 = 四条边里只留一条有颜色,其余透明 → 一个实心三角形
        "QScrollBar::up-arrow:vertical, QScrollBar::down-arrow:vertical {"
        "  background: none; width: 0; height: 0;"
        "  border-left: 4px solid transparent; border-right: 4px solid transparent;"
        "}"
        "QScrollBar::up-arrow:vertical { border-bottom: 6px solid %11; }"
        "QScrollBar::down-arrow:vertical { border-top: 6px solid %11; }"
        "QScrollBar:horizontal {"
        "  background: %6; height: 15px; border: none;"
        "  margin: 0px 16px 0px 16px;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: %7; border-radius: 3px; min-width: 24px;"
        "  margin: 2px 0px 2px 0px;"
        "}"
        "QScrollBar::handle:horizontal:hover { background: %8; }"
        "QScrollBar::add-line:horizontal {"
        "  background: %9; border: none; width: 16px;"
        "  subcontrol-position: right; subcontrol-origin: margin;"
        "}"
        "QScrollBar::sub-line:horizontal {"
        "  background: %9; border: none; width: 16px;"
        "  subcontrol-position: left; subcontrol-origin: margin;"
        "}"
        "QScrollBar::add-line:horizontal:hover, QScrollBar::sub-line:horizontal:hover {"
        "  background: %10;"
        "}"
        "QScrollBar::left-arrow:horizontal, QScrollBar::right-arrow:horizontal {"
        "  background: none; width: 0; height: 0;"
        "  border-top: 4px solid transparent; border-bottom: 4px solid transparent;"
        "}"
        "QScrollBar::left-arrow:horizontal { border-right: 6px solid %11; }"
        "QScrollBar::right-arrow:horizontal { border-left: 6px solid %11; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"
        "QSlider::groove:horizontal { height: 4px; background: %12; border-radius: 2px; }"
        "QSlider::handle:horizontal {"
        "  width: 13px; height: 13px; margin: -5px 0;"
        "  background: %5; border-radius: 7px;"
        "}"
        "QSlider::handle:horizontal:hover { background: #5B9BFF; }"
        "QToolTip {"
        "  background: %3; color: %1; font-size: 11px;"
        "  border: 1px solid %4; border-radius: 4px; padding: 5px 8px;"
        "}"
        "QLineEdit {"
        "  background: %13; color: %1;"
        "  border: 1px solid %12; border-radius: 5px; padding: 3px 9px;"
        "}"
        "QLineEdit:focus { border-color: %5; }"
        "QSplitter::handle { background: %4; }"
        "QSplitter::handle:horizontal { width: 1px; }"
        // ── 下拉框 / 数字框:箭头全应用统一 ─────────────────────────────
        // 只写 ::down-arrow 是不够的:样式表一旦存在,Qt 就要自己画 drop-down 那一块,
        // 而它**没有 background 时用的是调色板 Base**(深色主题下 = 一块白),
        // 三角被盖在里面 = 用户看到的"一个白色实心长方形按钮,没有下箭头"。
        // 这里显式给透明底 + 与滚动条/标签滚动按钮同款的边框三角(同一个 %11 箭头色),
        // 所有 QComboBox / QSpinBox / QDoubleSpinBox 一次性跟着改,不再各页各写一份。
        "QComboBox::drop-down {"
        "  width: 18px; border: none; background: transparent;"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "}"
        "QComboBox::drop-down:hover { background: %16; }"
        "QComboBox::down-arrow {"
        "  image: none; width: 0; height: 0; background: none;"
        "  border-left: 4px solid transparent; border-right: 4px solid transparent;"
        "  border-top: 5px solid %11; margin-right: 6px;"
        "}"
        "QAbstractSpinBox::up-button, QAbstractSpinBox::down-button {"
        "  width: 14px; border: none; background: transparent;"
        "}"
        "QAbstractSpinBox::up-arrow, QAbstractSpinBox::down-arrow {"
        "  width: 0; height: 0; background: none;"
        "  border-left: 3px solid transparent; border-right: 3px solid transparent;"
        "}"
        "QAbstractSpinBox::up-arrow { border-bottom: 4px solid %11; }"
        "QAbstractSpinBox::down-arrow { border-top: 4px solid %11; }"
        // ── 查看器标签条 ──
        // 必须逐子控件写:上面那条 QWidget 规则只管底色+纯白文字,tabs 交给
        // windowsvista 自己画就是"浅灰 tab + 白字" = 一条读不出字的空白栏
        "QTabBar { background: %3; border: none; }"
        "QTabBar::tab {"
        "  background: %14; color: %1;"
        "  border: 1px solid %4; border-bottom: none;"
        "  border-top-left-radius: 5px; border-top-right-radius: 5px;"
        "  margin-top: 3px; padding: 4px 4px 4px 10px;"
        "  min-width: 72px; max-width: 190px;"
        "}"
        "QTabBar::tab:selected {"
        "  background: %15; margin-top: 2px;"
        "  border-top: 2px solid %5;"
        "}"
        "QTabBar::tab:hover:!selected { background: %16; }"
        // 标签溢出时的左右滚动按钮:箭头与滚动条同款(边框三角形),不自带图标
        "QTabBar::scroller { width: 32px; margin: 0; }"
        "QTabBar QToolButton { background: %9; border: none; }"
        "QTabBar QToolButton:hover { background: %10; }"
        "QTabBar QToolButton::left-arrow {"
        "  width: 0; height: 0; background: none;"
        "  border-top: 4px solid transparent; border-bottom: 4px solid transparent;"
        "  border-right: 6px solid %11;"
        "}"
        "QTabBar QToolButton::right-arrow {"
        "  width: 0; height: 0; background: none;"
        "  border-top: 4px solid transparent; border-bottom: 4px solid transparent;"
        "  border-left: 6px solid %11;"
        "}"
        // ── #89 样式收敛:原散在各控件构造期的静态内联 setStyleSheet 并入本表。
        // 语义与原内联表逐条一致,但级联从"控件表必赢"变为"同特异性靠位置",
        // 因此块内顺序敏感:工具条后代泛规则在前,#addrBar/#fmtFilterCombo/
        // 小箭头钮靠排在其后赢回自己的属性;后代泛规则刻意保留 QWidget 后代
        // 匹配(分隔线/弹层容器当年吃到的 background 原样复刻,防像素漂移)。
        "QWidget#addrRow, QWidget#toolRow,"
        "QWidget#addrRow QWidget, QWidget#toolRow QWidget {"
        "  background: %3; border-bottom: 1px solid %4;"
        "}"
        "QWidget#addrRow QToolButton, QWidget#toolRow QToolButton {"
        "  background: transparent; border: none; border-radius: 4px;"
        "  padding: 3px 6px; color: %1; font-size: 11px;"
        "}"
        "QWidget#addrRow QToolButton:hover, QWidget#toolRow QToolButton:hover {"
        "  background: %16;"
        "}"
        "QWidget#addrRow QToolButton::menu-indicator,"
        "QWidget#toolRow QToolButton::menu-indicator { image: none; }"
        "QToolButton#barArrowBtn { color: %11; font-size: 9px; }"
        "QLineEdit#addrBar {"
        "  background: %13; color: %1; border: 1px solid %12;"
        "  border-radius: 4px; padding: 2px 8px; font-size: 11px;"
        "}"
        "QLabel#fmtComboArrow { color: %11; background: transparent; font-size: 9px; }"
        "QWidget#paneHdr, QWidget#paneHdr QWidget {"
        "  background: %20; border-bottom: 1px solid %4;"
        "}"
        "QWidget#paneHdr QLabel {"
        "  background: transparent; color: %1; font-size: 12px;"
        "}"
        "QWidget#paneHdr QToolButton {"
        "  background: transparent; border: none; border-radius: 4px;"
        "  color: %1; font-size: 13px;"
        "}"
        "QWidget#paneHdr QToolButton:hover { background: %16; }"
        "QWidget#treePane, QWidget#favPane, QWidget#filterPane {"
        "  background: %14; border: none;"
        "}"
        "QWidget#previewPane, QWidget#infoPane {"
        "  background: %15; border: none;"
        "}"
        "QStatusBar {"
        "  background: %21; border-top: 1px solid %4;"
        "  color: %1; font-size: 11px; padding: 2px 10px;"
        "}"
        "QStatusBar::item { border: none; }"
        "QLabel#statusLabel, QLabel#pathLabel { color: %1; background: transparent; }"
        "QMenuBar {"
        "  background: %19; color: %1; font-size: 12px;"
        "  padding: 3px 2px; border-bottom: 1px solid %4;"
        "}"
        "QMenuBar::item { background: transparent; padding: 4px 10px; border-radius: 3px; }"
        "QMenuBar::item:selected { background: %16; }"
        "QMenuBar::item:pressed { background: %5; color: #FFF; }"
        "QToolButton#tabCloseBtn {"
        "  border: none; background: transparent; color: %22;"
        "  font-size: 14px; padding: 0 2px;"
        "}"
        "QToolButton#tabCloseBtn:hover { color: %1; background: %16; border-radius: 3px; }"
        "QToolButton#filmNavBtn {"
        "  background: rgba(24,24,30,215); border: 1px solid #3A3A42;"
        "  border-radius: 10px;"
        "}"
        "QToolButton#filmNavBtn:hover { background: #3A3A42; border-color: #6A6A74; }"
        "QLabel#filmDragHint {"
        "  background: rgba(24,24,30,235); color: #FFFFFF;"
        "  border: 1px solid #3A3A42; border-radius: 4px;"
        "  padding: 3px 8px; font-size: 12px;"
        "}"
        "QFrame#toolSep { color: %24; }"
        "QComboBox#fmtFilterCombo {"
        "  background: %3; color: %1; border: 1px solid %4;"
        "  border-radius: 4px; padding: 2px 10px; font-size: 12px; min-height: 22px;"
        "}"
        "QComboBox#fmtFilterCombo:hover { border-color: %23; }"
        "QComboBox#fmtFilterCombo:focus { border-color: %5; }"
        "QComboBox#fmtFilterCombo::drop-down {"
        "  width: 18px; border: none; background: transparent;"
        "  subcontrol-origin: padding; subcontrol-position: top right;"
        "}"
        "QComboBox#fmtFilterCombo::down-arrow {"
        "  image: none; width: 0; height: 0; background: none; border: none;"
        "}"
        "QComboBox#fmtFilterCombo QAbstractItemView {"
        "  background: %13; color: %1; border: 1px solid %4;"
        "  selection-background-color: %5; outline: none;"
        "}"
        "QComboBox#fmtFilterCombo QAbstractItemView::item {"
        "  min-height: 24px; padding: 2px 8px;"
        "}"
        // ── 对话框按钮(2026-09-03 用户令:确定/取消不够醒目,边框与背景融为一体)──
        // 全部对话框统一成「次按钮=底色+蓝色描边,默认按钮=蓝底白字」,
        // 与设置对话框既有按钮样式同口径(那里是内联写死的,这里是全局兜底)。
        // 必须放在 QMessageBox 之前:两条规则特异性相同,后者赢 —— 消息框
        // 仍是它自己的老样式。
        "QDialog QPushButton {"
        "  background: %16; color: %1;"
        "  border: 1px solid %5; padding: 6px 18px; border-radius: 4px;"
        "}"
        "QDialog QPushButton:hover { background: %17; }"
        "QDialog QPushButton:default {"
        "  background: %5; border-color: %5; color: #FFF;"
        "}"
        "QDialog QPushButton:default:hover { background: %18; }"
        "QMessageBox { background: %2; color: %1; }"
        "QMessageBox QLabel { color: %1; background: transparent; }"
        "QMessageBox QPushButton {"
        "  background: %16; color: %1;"
        "  border: 1px solid %12; padding: 6px 18px; border-radius: 4px;"
        "}"
        "QMessageBox QPushButton:hover {"
        "  background: %17; border-color: %5;"
        "}"
        // ── #89 收敛(批3):views 层静态样式(文件网格/查找条/重命名框/排序表头/
        // 全屏胶片条/文件卡片)。类型选择器用自定义类名(FileGrid/FileCanvas/
        // SortHeader/FileCard),普通容器靠 objectName。全屏胶片条与 LIVE 徽章
        // 沿用原内联的硬编码深色(浮层,不随主题换档)。
        "FileGrid { background: %13; border: none; }"
        "FileCanvas { background: %13; }"
        "QWidget#findBar {"
        "  background: %3; border: 1px solid %4; border-radius: 4px;"
        "}"
        "QWidget#findBar QLineEdit {"
        "  background: %13; color: %1; border: 1px solid %4;"
        "  border-radius: 3px; padding: 1px 6px; selection-background-color: %5;"
        "}"
        "QWidget#findBar QToolButton {"
        "  background: transparent; border: none; border-radius: 3px;"
        "}"
        "QWidget#findBar QToolButton:hover { background: %16; }"
        "QWidget#findBar QToolButton:pressed { background: %4; }"
        "QWidget#findBar QToolButton:disabled { background: transparent; }"
        "QLabel#findInfo {"
        "  color: %25; font-size: 12px; background: transparent; border: none;"
        "}"
        "QLineEdit#renameEdit {"
        "  background: %13; color: %1; border: 1px solid %5;"
        "  font-size: 12px; padding: 0 2px;"
        "}"
        // 排序表头:原内联是裸声明(背景+下边线泼给全部子孙,含列钮与垫片),
        // 这里用 QWidget 后代选择器原样复刻,列钮规则紧随其后靠排位赢回透明底
        "SortHeader, SortHeader QWidget {"
        "  background: %3; border-bottom: 1px solid %4;"
        "}"
        "SortHeader QPushButton {"
        "  background: transparent; color: %1; border: none;"
        "  padding: 2px 8px; font-size: 11px; text-align: left; border-radius: 4px;"
        "}"
        "SortHeader QPushButton:hover { color: %1; background: %16; }"
        // 全屏胶片条:浮层,原内联硬编码深色,不随主题
        "QWidget#filmStrip {"
        "  background: rgba(18,18,24,235); border: 1px solid #3A3A42; border-radius: 8px;"
        "}"
        "QWidget#filmStrip::viewport { background: transparent; }"
        "QWidget#filmBtnBar QToolButton {"
        "  background: transparent; border: none; border-radius: 4px;"
        "  padding: 2px 6px; font-size: 11px; color: #9A9AA4;"
        "}"
        "QWidget#filmBtnBar QToolButton:hover { background: #3A3A42; color: #FFFFFF; }"
        "QWidget#filmBtnBar QToolButton:checked {"
        "  background: rgba(0,120,215,80); color: #FFFFFF;"
        "}"
        "QWidget#filmBtnBar QToolButton#filmClose { padding: 0; }"
        "QLabel#filmCaption {"
        "  background: transparent; color: #FFFFFF; font-size: 13px; font-weight: 600;"
        "}"
        // 文件卡片:本体透明(图片之外纯黑由卡片父级透出)
        "FileCard { background: transparent; border: none; }"
        "QLabel#cardThumb { background: transparent; }"
        "QLabel#cardLiveBadge {"
        "  background: rgba(0,0,0,150); color: #FFF; font-size: 9px; font-weight: bold;"
        "  padding: 2px 7px; border-radius: 8px; border: 1px solid rgba(255,255,255,60);"
        "}"
    )
        .arg(C_TEXT)
        .arg(C_WIN_BG)
        .arg(C_TOOLBAR)
        .arg(C_SEPARATOR)
        .arg(C_ACCENT)
        .arg(C_SB_TRACK)
        .arg(C_SB_HANDLE)
        .arg(C_SB_HANDLE_H)
        .arg(C_SB_BUTTON)
        .arg(C_SB_BUTTON_H)
        .arg(C_SB_ARROW)
        .arg(C_CARD_BORDER)
        .arg(C_CONTENT)
        .arg(C_SIDEBAR)
        .arg(C_PREVIEW_BG)
        .arg(C_CARD_HOVER)
        .arg(C_SEPARATOR)      // %17:消息框按钮悬停(#215 起 C_PANE_HDR=#191919 不再作 hover,换描边灰=比按钮底亮一档)
        .arg(C_ACCENT_DOWN)    // %18:默认(确定)按钮悬停/按下,比 C_ACCENT 暗一档
        .arg(C_MENUBAR)        // %19:菜单栏底(#89 收敛自 mainwindow_menus 内联)
        .arg(C_PANE_HDR)       // %20:面板标题条底(#89 收敛自 createPaneHeader 内联)
        .arg(C_STATUSBAR)      // %21:状态栏底(#89 收敛自 createStatusbar 内联)
        .arg(C_TEXT_HIDDEN)    // %22:标签关闭钮常态字色(#89 收敛自 installTabCloseButton 内联)
        .arg(Theme::T("#4A4A56", "#9A9AA4"))  // %23:格式筛选框悬停描边(原内联局部双档值)
        .arg(Theme::T("#2A2A2E", "#C9C9D1"))  // %24:工具条竖分隔线(原内联局部双档值)
        .arg(C_TEXT_SUB);      // %25:查找条计数文字(#89 收敛自 filegrid_find 内联)
}

} // namespace Theme
