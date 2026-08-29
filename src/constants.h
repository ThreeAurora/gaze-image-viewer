#pragma once
#include <QString>
#include <unordered_set>

// ── 设计令牌：深色分层调色板(对标 XnView MP 层次感) ──
#define C_WIN_BG        "#1B1B1F"   // 窗体底
#define C_SIDEBAR       "#17171A"   // 文件夹树
#define C_CONTENT       "#1F1F23"   // 内容区
#define C_PREVIEW_BG    "#000000"   // 预览面板(纯黑)
#define C_CARD_BG       "#26262B"   // 卡片常态
#define C_CARD_HOVER    "#2C2C32"   // 卡片悬停
#define C_THUMB_BG      "#141417"   // 卡片缩略图衬底
#define C_CARD_BORDER   "#303036"   // 卡片边框
#define C_TOOLBAR_BG    "#1B1B1F"
#define C_STATUSBAR_BG  "#17171A"
#define C_ACCENT        "#3B82F6"   // 强调蓝:选中/焦点/进度
#define C_ACCENT_DOWN   "#2F6FE0"   // 主按钮悬停/按下(比 C_ACCENT 暗一档)
#define C_ACCENT_DOWN   "#2F6FE0"   // 主按钮悬停/按下(比 C_ACCENT 暗一档)
#define C_ACCENT_DOWN   "#2F6FE0"   // 主按钮悬停/按下(比 C_ACCENT 暗一档)
#define C_ACCENT_DOWN   "#2F6FE0"   // 主按钮悬停/按下(比 C_ACCENT 暗一档)
#define C_SELECT_BLUE   "#3B82F6"
#define C_SELECT_YELLOW "#E8B339"
#define C_SEPARATOR     "#2A2A2E"
#define C_TREE_TEXT     "#D6D6DC"
#define C_TREE_HOVER    "#26262B"
#define C_TREE_SELECT   "#3B82F6"
#define C_CANVAS_BG     0x141417    // 视频缩略图画布(数值型,thumbnailer 用)
#define C_TEXT          "#FFFFFF"   // 主文字(最纯白)
#define C_TEXT_SUB      "#FFFFFF"   // 次级文字(纯白)
#define C_TEXT_DIM      "#FFFFFF"   // 弱文字(纯白)

// 类型角标(降饱和橙黄系,右下角胶囊)
#define C_VIDEO_BG "#B25E00"
#define C_GIF_BG   "#8F7D00"
#define C_OTHER_BG "#191919"

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
inline const std::unordered_set<QString> IMAGE_EXTS = {
    ".jpg", ".jpeg", ".png", ".gif", ".bmp", ".webp",
    ".heic", ".heif", ".tiff", ".tif", ".ico", ".svg"
};

inline const std::unordered_set<QString> VIDEO_EXTS = {
    ".mp4", ".mov", ".avi", ".mkv", ".webm", ".wmv", ".flv",
    ".m4v", ".mpg", ".mpeg", ".3gp", ".ts", ".m2ts", ".mts",
    ".vob", ".ogv", ".divx", ".rm", ".rmvb", ".asf", ".f4v",
    ".avchd", ".mxf", ".qt"
};

inline const std::unordered_set<QString> AUDIO_EXTS = {
    ".mp3", ".wav", ".flac", ".aac", ".ogg", ".wma", ".m4a", ".opus"
};

inline const std::unordered_set<QString> DOCUMENT_EXTS = {
    ".txt", ".doc", ".docx", ".pdf", ".rtf", ".odt", ".xls", ".xlsx",
    ".ppt", ".pptx", ".csv", ".md", ".tex", ".epub", ".mobi"
};

inline const std::unordered_set<QString> EXECUTABLE_EXTS = {
    ".exe", ".bat", ".cmd", ".ps1", ".sh", ".msi", ".com", ".scr",
    ".jar", ".py", ".pl", ".rb"
};

inline const std::unordered_set<QString> DOCUMENT_EXTS = {
    ".txt", ".doc", ".docx", ".pdf", ".rtf", ".odt", ".xls", ".xlsx",
    ".ppt", ".pptx", ".csv", ".md", ".tex", ".epub", ".mobi"
};

inline const std::unordered_set<QString> EXECUTABLE_EXTS = {
    ".exe", ".bat", ".cmd", ".ps1", ".sh", ".msi", ".com", ".scr",
    ".jar", ".py", ".pl", ".rb"
};
