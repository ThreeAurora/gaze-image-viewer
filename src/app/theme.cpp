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
    // 占位符按 %1..%17 顺序逐个 .arg:单个 arg() 每次替换最小编号占位符,
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
        // ── 菜单勾选指示器(2026-09-04 用户令:打勾框要更明显,得让人看出"能勾")──
        // 原生 windowsvista 的对勾在深色菜单上几乎隐形;未勾=空框(预告可勾),
        // 勾中=主题色实心框+白对勾(两主题都读得出)。image 用编译进 qrc 的
        // menu_check.png(自有资产,不碰 assets/icons-48 的 XnView 图)。
        "QMenu::indicator { width: 15px; height: 15px; margin-left: 5px; }"
        "QMenu::indicator:unchecked {"
        "  border: 1px solid %11; border-radius: 4px; background: transparent;"
        "}"
        "QMenu::indicator:disabled { border: 1px solid %4; border-radius: 4px; }"
        "QMenu::indicator:checked {"
        "  background: %5; border: 1px solid %5; border-radius: 4px;"
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
        .arg(C_ACCENT_DOWN);   // %18:默认(确定)按钮悬停/按下,比 C_ACCENT 暗一档
}

} // namespace Theme
