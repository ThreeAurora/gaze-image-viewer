#pragma once
#include <QString>

// ── 主题:深/浅双色运行时选择(#96) ──
//   启动时 Theme::init() 读 Appearance/theme 一次("dark"|"light"),此后所有
//   颜色经 constants.h 的 C_* 宏 → Theme::T(深值, 浅值) 取值。切换主题需重启
//   ——各控件样式表在构造期拼接、绘制路径缓存颜色,活应用要重跑全部样式函数,
//   得不到诚实保证,故设置页明确标注"重启后生效"。
//   深色值逐字保持原调色板(已验收外观不动);浅色为新增档。
namespace Theme {

void init();                 // main() 里 setStyleSheet 前调用一次
bool light();                // 当前是否浅色

inline const char* T(const char* dark, const char* light) {
    return light() ? light : dark;
}

QString appQss();            // 应用级 QSS(自 main.cpp 收编,含全套令牌取值)

} // namespace Theme
