#pragma once
#include <QString>
#include <QHash>   // 必须先于 <unordered_set>:下面 IMAGE_EXTS 等实例化 unordered_set<QString>
                   // 时 std::hash<QString> 特化必须已存在,否则在先实例化后特化直接编译失败
#include <unordered_set>
#include "theme.h"

// ── 设计令牌:深色分层调色板(对标 XnView MP 的灰阶层级,不再全纯黑) ──
//   由外到内逐层变暗:菜单栏 > 工具条 > 树 > 列表 > 预览
//   缩略图/画布仍保持近黑:图看久了不偏色,且 thumbnails.db 里已烘焙的
//   视频封面是纯黑 letterbox,衬底改灰会露出两层颜色
// #96:每个令牌 = Theme::T(深色值, 浅色值)。深色值逐字保持已验收调色板;
//   浅色档为 2026-08-31 新增,由外到内逐层变亮(菜单栏 > 工具条 > 树 > 列表 > 预览),
//   内容区纯白。切换走 Appearance/theme,重启生效。
#define C_WIN_BG        Theme::T("#212126", "#F3F3F5")   // 窗体底
#define C_MENUBAR       Theme::T("#31313A", "#F9F9FB")   // 菜单栏(最亮一层)
#define C_TOOLBAR       Theme::T("#2A2A31", "#ECECEF")   // 工具条/地址栏/列表表头
#define C_STATUSBAR     Theme::T("#25252B", "#F0F0F3")   // 状态栏
#define C_SIDEBAR       Theme::T("#212126", "#F3F3F5")   // 文件夹树
#define C_CONTENT       Theme::T("#1A1A1F", "#FFFFFF")   // 内容区(文件列表底)
#define C_PREVIEW_BG    Theme::T("#141418", "#E9E9ED")   // 预览面板(近黑)
#define C_PANE_HDR      Theme::T("#35353E", "#E3E3E9")   // 面板标题条("文件夹"/"预览")
#define C_CARD_BG       Theme::T("#232328", "#FFFFFF")   // 卡片常态
#define C_CARD_HOVER    Theme::T("#2E2E35", "#EDF2FB")   // 卡片悬停
#define C_THUMB_BG      Theme::T("#141418", "#E9E9ED")   // 卡片缩略图衬底
#define C_CARD_BORDER   Theme::T("#30303A", "#D9D9E0")   // 卡片边框
#define C_SEPARATOR     Theme::T("#3A3A44", "#C9C9D1")   // 分隔线/描边(灰阶层,比旧值提亮)
#define C_ACCENT        Theme::T("#3B82F6", "#3B82F6")   // 强调蓝:选中/焦点/进度
#define C_ACCENT_DOWN   Theme::T("#2F6FE0", "#2F6FE0")   // 主按钮悬停/按下(比 C_ACCENT 暗一档)
// 选中蓝两档(2026-09-02 用户定版):亮蓝=正被操作(树/网格持有焦点),暗蓝=失焦残留
#define C_SELECT_BLUE   Theme::T("#0078D7", "#0078D7")   // rgb(0,120,215) 亮蓝
#define C_SELECT_DIM    Theme::T("#2164A8", "#2164A8")   // rgb(33,100,168) 暗蓝
#define C_SELECT_YELLOW Theme::T("#E8B339", "#E8B339")
#define C_TREE_TEXT     Theme::T("#FFFFFF", "#1F1F26")
#define C_TREE_HOVER    Theme::T("#2E2E35", "#E4E7EE")
#define C_TREE_SELECT     C_SELECT_BLUE   // 树有焦点:亮蓝(当前操作对象)
#define C_TREE_SELECT_DIM C_SELECT_DIM    // 树失焦:暗蓝(视觉残留)
#define C_TEXT_HIDDEN   Theme::T("#8F8F8F", "#9C9CA6")   // 隐藏文件/文件夹：淡灰（Windows 风格弱化显示）
#define C_CANVAS_BG     0x000000    // 视频缩略图画布(数值型,thumbnailer 用;入库不可回改,双主题恒黑)
#define C_TEXT          Theme::T("#FFFFFF", "#1F1F26")   // 主文字
#define C_TEXT_SUB      Theme::T("#FFFFFF", "#44444C")   // 次级文字
#define C_TEXT_DIM      Theme::T("#FFFFFF", "#77777F")   // 弱文字
#define C_TEXT_SOFT     Theme::T("#E8E8E8", "#3F3F49")   // 组标题(深值=旧字面量逐字保留,浅色可读)
#define C_TEXT_FAINT    Theme::T("#B8B8B8", "#8A8A94")   // 状态弱文字(同上)

// ── 滚动条(带上下/左右箭头按钮的 Win 风格) ──
#define C_SB_TRACK      Theme::T("#24242A", "#EBEBEE")   // 轨道
#define C_SB_HANDLE     Theme::T("#55555F", "#B6B6BF")   // 滑块
#define C_SB_HANDLE_H   Theme::T("#6E6E7A", "#9A9AA4")   // 滑块悬停
#define C_SB_BUTTON     Theme::T("#2E2E36", "#E4E4E8")   // 箭头按钮底
#define C_SB_BUTTON_H   Theme::T("#41414B", "#D3D3D9")   // 箭头按钮悬停
#define C_SB_ARROW      Theme::T("#C2C2CA", "#55555E")   // 箭头(边框三角形画的)

// 类型角标(降饱和橙黄系,右下角胶囊;深底色胶囊双主题都压得住,浅色下稍柔)
#define C_VIDEO_BG Theme::T("#B25E00", "#B25E00")
#define C_GIF_BG   Theme::T("#8F7D00", "#8F7D00")
#define C_OTHER_BG Theme::T("#191919", "#63636B")

// ── 缩略图卡片宽度区间 ──
// 所有入口共用(尺寸菜单预设 / 自定义对话框 / Ctrl+= 缩放 / 滚轮 / 外观页数值框)。
// 各处各写一份区间就是 #68 的病根:setCardSize 收 [80,300],而菜单里摆着
// 64/384/768 —— 用户点了标签写着 384x288,实际拿到的是 300,且看不出来。
#define THUMB_W_MIN   48
#define THUMB_W_MAX   1024

// ── 快速幻灯片间隔区间(毫秒) ──
// 设置页数值框与 mainwindow 的两处 ini 读取共用。区间只写在设置页时,读侧
// 就形同"随便信 ini":手改/写坏的 slideInterval 能把定时器变成 14 天不响
// 或 100ms 一跳(实测见 cache/tmp/combo_placeholder_test.cpp 事实A)。
#define SLIDE_MS_MIN   100
#define SLIDE_MS_MAX   60000
#define SLIDE_MS_DEF   1000

// ── 扩展名白名单 ──
// avif/avifs/jxl:Qt 无原生插件(MSVC ABI 不兼容,#116),走 ffmpeg 分流;
// hif 是 HEIF 的 Nokia 变体扩展名;heic/heif/hif 均走 ffmpeg 自带解码(#8,
// 空图回退 WIC)。
// tga/icns/wbmp/pbm/pgm/ppm/xbm/xpm/svgz/cur/jfif:#103 §3.1a 矩阵盘点(2026-09-01)
// 发现 Qt 6.8.3 部署插件已能解(探针 21 格式实测)但白名单漏收 —— 纯白名单补收,
// 走原生策略 1-3,无需分流。
inline const std::unordered_set<QString> IMAGE_EXTS = {
    ".jpg", ".jpeg", ".jfif", ".png", ".gif", ".bmp", ".webp",
    ".heic", ".heif", ".hif", ".avif", ".avifs", ".jxl",
    ".tiff", ".tif", ".ico", ".svg", ".svgz",
    ".tga", ".icns", ".wbmp", ".pbm", ".pgm", ".ppm", ".xbm", ".xpm", ".cur",
    // #103 工单②(2026-09-01 用户裁决"该支持就支持,全部支持"):六个全走
    // ffmpeg 分流(foreignimg)。qoi/dpx/apng 样张实测解码通过(cache/tmp/
    // verify103);exr/dds/jp2 ffmpeg native decoder 在位但无 encoder、样张
    // 未造 —— 声称未验证。
    ".exr", ".dds", ".qoi", ".jp2", ".dpx", ".apng"
};

// ── RAW 相机原始格式白名单(#140)──
// 独立于 IMAGE_EXTS:RAW 绝不进 Qt 解码/缩略图/预读管线(全解是数秒级重活)。
// 预览形态 = 占位说明 + 「加载原始RAW」按钮,点击才后台全解(rawdecode.h,
// 自带 LibRaw)。全囊括铁令(2026-09-01):不默认系统装有任何 RAW 解码器。
inline const std::unordered_set<QString> RAW_EXTS = {
    ".cr2", ".cr3", ".crw", ".nef", ".nrw", ".arw", ".srf", ".sr2",
    ".dng", ".orf", ".rw2", ".raf", ".pef", ".erf", ".rwl",
    ".3fr", ".fff", ".gpr", ".kdc", ".k25", ".mef", ".mrw",
    ".x3f", ".mos", ".srw", ".iiq"
};

inline const std::unordered_set<QString> VIDEO_EXTS = {
    ".mp4", ".mov", ".avi", ".mkv", ".webm", ".wmv", ".flv",
    ".m4v", ".mpg", ".mpeg", ".3gp", ".ts", ".m2ts", ".mts",
    ".vob", ".ogv", ".divx", ".rm", ".rmvb", ".asf", ".f4v",
    ".avchd", ".mxf", ".qt",
    // #103 工单④(2026-09-01 用户裁决全加):3g2/ogm 样张实测解析通过,
    // m1v/m2v 裸 ES 流抽帧实测通过(ffprobe duration 对裸流不可靠)。
    // .avchd 保留:它是 BDMV 盘片目录格式,作文件扩展名几乎不出现,
    // 移除零收益、误伤风险不为零 → 不动。
    ".3g2", ".ogm", ".m1v", ".m2v"
};

inline const std::unordered_set<QString> AUDIO_EXTS = {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".wma", ".m4a", ".opus",
    // #103 工单③(2026-09-01 用户裁决全加):amr(须 8kHz)与 ac3 样张均
    // 实测解码通过;列表内播放走 Qt Multimedia ffmpeg 后端,真机播放待点验。
    ".amr", ".ac3"
};

inline const std::unordered_set<QString> DOCUMENT_EXTS = {
    ".txt", ".doc", ".docx", ".pdf", ".rtf", ".odt", ".xls", ".xlsx",
    ".ppt", ".pptx", ".csv", ".md", ".tex", ".epub", ".mobi"
};

inline const std::unordered_set<QString> EXECUTABLE_EXTS = {
    ".exe", ".bat", ".cmd", ".ps1", ".sh", ".msi", ".com", ".scr",
    ".jar", ".py", ".pl", ".rb"
};
