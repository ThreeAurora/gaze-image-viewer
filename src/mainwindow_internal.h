#pragma once
// mainwindow 一族编译单元(mainwindow.cpp / mainwindow_menus.cpp / mainwindow_nav.cpp
// / mainwindow_tabs.cpp / mainwindow_viewer.cpp / mainwindow_keys.cpp
// / mainwindow_ops.cpp)共用的文件级 helper。
// 这些符号原先都是 mainwindow.cpp 里的 namespace 作用域 static(内部链接)。
// 拆分后它们被多个编译单元引用,而 static 数据跨 TU 会变成每 TU 一份实例,
// 所以整体搬到这里:函数用 inline,数据用 C++17 inline 变量 —— 全程序单实例,
// 语义与拆分前一致。调用点只加 mw_impl:: 限定前缀,函数体逐字未改。
// 只被单个编译单元用的(如 kPanes/kPaneCount)仍然留在那一个 .cpp 里。
#include "constants.h"
#include "filegrid.h"    // FILTER_* 枚举(kFilterModes 表的初值)
#include "settings.h"    // AppSettings(appSettings()/slideIntervalMs 读同一份 ini)
#include <QDir>
#include <QList>
#include <QSettings>
#include <QString>

namespace mw_impl {

// 统一设置存储:与设置面板(AppSettings)共用便携 ini(exe 目录/Gaze.ini)
// 此前用 QSettings 默认构造(注册表),因 main.cpp 未 setOrganizationName
// 导致 AccessError 全部静默失效(布局/快捷键/最近文件/路径历史都不生效)
inline QSettings appSettings() {
    // 与 AppSettings 同一个文件:位置可能已被 Integration/iniLocation 改到
    // %APPDATA% 或自定义目录,这里不能再硬编码 exe 目录(会写回便携那份)
    return QSettings(AppSettings::instance().iniPath(), QSettings::IniFormat);
}

// 快速幻灯片间隔:ini 是用户可手改的文件,读它不能只 qMax(100, toInt())。
// 实测(Qt 6.5.3,cache/tmp/combo_placeholder_test.cpp 事实A):
//   · "99999999999" 不报错,而是回绕成 1215752191 → 定时器约 14 天响一次,
//     幻灯片看着就是"按了没反应";
//   · 非数字("abc"/空/"2e3")toInt()=0 → 旧判据 qMax(100,0) 把它顶成 100ms
//     最快档,而不是退回默认 1000。
// 统一收在设置页声明的 [SLIDE_MS_MIN, SLIDE_MS_MAX] 内,读不出数字用默认值。
inline int slideIntervalMs() {
    bool ok = false;
    const int v = AppSettings::instance()
                      .get("Interface/slideInterval", SLIDE_MS_DEF).toInt(&ok);
    return ok ? qBound(SLIDE_MS_MIN, v, SLIDE_MS_MAX) : SLIDE_MS_DEF;
}

// 筛选模式 → 名称。这张表原先只活在 createFilterMenu 里,工具栏格式下拉框想报出
// "当前是什么筛选"就只能自己再抄一份(#73 的误导就是这么来的)。
// mode<0 是"自定义(即将支持)"占位项,不参与查名。
struct FilterEntry { int mode; const char* name; };

// #105:「浏览器」常驻标签的 tabData 哨兵(真文件的绝对路径不可能等于它)。
// 定义在 ctor 之前:currentChanged/tabMoved 的 ctor lambda 都要查它。
inline const QString kBrowserTabData = QStringLiteral("__browser__");

inline const FilterEntry kFilterModes[] = {
    {FILTER_ALL,         "全部"},
    {FILTER_IMAGES,      "图像"},
    {FILTER_IMAGES_DIRS, "图像(+目录)"},
    {FILTER_VIDEOS,      "视频"},
    {FILTER_VIDEOS_DIRS, "视频(+目录)"},
    {FILTER_AUDIO,       "音频"},
    {FILTER_ARCHIVES,    "压缩文件"},
    {FILTER_DOCUMENTS,   "文档"},
    {FILTER_EXECUTABLES, "可执行文件"},
    {FILTER_FOLDERS,     "文件夹"},
    {FILTER_CUSTOM,      "自定义(扩展名…)"},   // #125:选中即弹框编辑自己的扩展名清单
    {FILTER_RED,         "红色"},
    {FILTER_ORANGE,      "橙色"},
    {FILTER_YELLOW,      "黄色"},
    {FILTER_GREEN,       "绿色"},
    {FILTER_BLUE,        "蓝色"},
    {FILTER_UNRED,       "非红色"},
};

inline QString filterModeName(int mode) {
    if (mode < 0) return QString();
    for (const FilterEntry& e : kFilterModes)
        if (e.mode == mode) return QString::fromUtf8(e.name);
    return QString();
}

// 存档分栏是否可信。顺序固定:0=树 1=网格 2=预览。
// 树/预览可以是 0 —— 那是用户用面板标题条 X 关面板的合法意图(见 kPanes);
// 网格不在可关面板之列,它等于 0 只可能是"在查看器模式里退出"留下的
// (toggleViewer 把 sizes 设成 {0,0,W}),这种存档回用会把布局永久锁死。
inline bool splitterArchiveUsable(const QList<int>& sz) {
    return sz.size() == 3 && sz[0] >= 0 && sz[1] > 0 && sz[2] >= 0;
}

// 解析 "a,b,c" 分栏存档;任一格不是整数即视为不可用
inline QList<int> parseSplitterSizes(const QString& v) {
    QList<int> out;
    for (const QString& part : v.split(',')) {
        bool ok = false;
        const int n = part.trimmed().toInt(&ok);
        if (!ok) return {};
        out << n;
    }
    return out;
}

// 出厂分栏默认值:优先用用户上次保存的布局(Layout/last/splitter,如 270,419,1233),
// 任何"重置回默认"的路径都不再用硬编码——用户的布局就是默认配置
inline QList<int> defaultSplitterSizes() {
    const QList<int> saved = parseSplitterSizes(
        appSettings().value("Layout/last/splitter").toString());
    if (splitterArchiveUsable(saved)) return saved;
    return {270, 570, 660};   // 从未保存过布局时的出厂兜底
}

// 路径显示/内部规范形:
//   内部一律用 '/' 且不带尾斜杠(历史栈 / lastDir / Browser/lastFile 比较都用它)
//   地址栏按 Windows 习惯显示:反斜杠 + 末尾 "\"(XnView 同款)
inline QString canonicalPath(const QString& raw) {
    QString p = QDir::fromNativeSeparators(raw.trimmed());
    while (p.size() > 3 && p.endsWith('/')) p.chop(1);   // "E:/" 根保留斜杠
    if (p.size() == 2 && p.endsWith(':')) p += '/';
    return p;
}

inline QString displayPath(const QString& canonical) {
    QString d = QDir::toNativeSeparators(canonical);
    if (!d.endsWith('\\')) d += '\\';
    return d;
}

} // namespace mw_impl
