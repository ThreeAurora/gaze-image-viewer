#pragma once
#include <QString>
#include <functional>
#include <vector>

// ── 主题:深/浅双色运行时选择(#96) ──
//   启动时 Theme::init() 读 Appearance/theme 一次("dark"|"light"),此后所有
//   颜色经 constants.h 的 C_* 宏 → Theme::T(深值, 浅值) 取值。
//   C_* 在 paintEvent/QSS 里展开的都在绘制期即时求值;但少数组件把颜色
//   "缓存"进了自己持有的字符串/画笔(构造期 setStyleSheet 的内联样式表、
//   填充期设进 QTreeWidgetItem 的前景色)。全局 setStyleSheet(Theme::appQss())
//   刷新不了这些,所以旧版只能"重启生效"。设置页切换主题的完整协议:
//     Theme::init(); app->setStyleSheet(Theme::appQss()); Theme::notifyChanged();
//   notifyChanged() 广播给各注册方,由它们重灌自己的缓存(见
//   MainWindow::applyThemeSurfaces / FolderTree::refreshThemeColors 等),
//   之后全局 update() 一重绘,主题即全量即时生效,不再需要重启。
//   深色值逐字保持原调色板(已验收外观不动);浅色为新增档。
namespace Theme {

void init();                 // main() 里 setStyleSheet 前调用一次;设置页切换也调用
bool light();                // 当前是否浅色

inline const char* T(const char* dk, const char* lt) {
    return light() ? lt : dk;   // 参数名避开 light():形参遮蔽会让分支永远取浅色值
}

QString appQss();            // 应用级 QSS(自 main.cpp 收编,含全套令牌取值)

// 主题切换通知:设置了"构造期缓存主题色"的组件注册处理器,切换入口在
// Theme::init() 之后调用 notifyChanged(),处理器负责重灌缓存色。
using ChangeHandler = std::function<void()>;
void addChangeHandler(ChangeHandler h);
void notifyChanged();

// 主题切换完整协议一步走(设置→外观 与 查看→主题 两个入口共用):
// init 重读双档标志 → notifyChanged 重灌各处缓存色 → 全局 QSS 重设 →
// 顶层窗体重绘刷新 T() 取色。调用前先把 Appearance/theme 落进 ini。
void applyLive();

} // namespace Theme
