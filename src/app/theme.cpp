#include "theme.h"
#include "settings.h"

namespace Theme {

namespace {
bool g_light = false;
}

void init() {
    g_light = AppSettings::instance().get("Appearance/theme", "dark").toString()
              == QLatin1String("light");
    // 后续诊断日志要能区分本次会话的主题
    if (g_light) qputenv("GAZE_THEME", "light");
}

bool light() { return g_light; }

QString appQss() {
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
        .arg(C_TEXT(), C_WIN_BG(), C_TOOLBAR(), C_SEPARATOR(), C_ACCENT(),
             C_SB_TRACK(), C_SB_HANDLE(), C_SB_HANDLE_H(), C_SB_BUTTON(),
             C_SB_BUTTON_H(), C_SB_ARROW(), C_CARD_BORDER(), C_CONTENT(),
             C_SIDEBAR(), C_PREVIEW_BG(), C_CARD_HOVER(), C_PANE_HDR());
}

} // namespace Theme
