#pragma once
#include <QString>
#include <QCoreApplication>

// ═══════════════════════════════════════════
// 全局翻译入口(2026-09-03 国际化 / 英文语言包)
// ═══════════════════════════════════════════
// 规则(给所有改串的代码看):
//   · 凡"用户可见"的字符串一律 gazeTr() 包裹 —— 菜单/按钮/提示/tooltip/
//     对话框标题与 label/消息框/状态栏文案等。
//   · 源串保留中文(即默认语言),英文译文由 exe 旁的 gaze_en.qm 提供:
//     运行时逐串解析,缺译的串以中文兜底;中文界面不装译者,零开销。
//   · 内部专用串(设置键、ini 分组、日志、扩展名、objectName 等)不包裹。
//   · 译文必须原样保留 %1/%2 占位符与 (&F) 助记符;助记符在同一菜单内唯一。
// 实现:QCoreApplication::translate("Gaze", ...) —— 单一 context 收编全部条目。
// lupdate 识别不了本包装函数,译文清单由 translations/i18n_build.py 正则提取、
// 合并翻译字典(translations/*.json)生成 gaze_en.ts → lrelease → gaze_en.qm。
inline QString gazeTr(const char* src) {
    return QCoreApplication::translate("Gaze", src);
}