#pragma once
// 查看器快捷键命令表 —— previewpanel(行为)与 settings_dialog(设置页)共用。
// 此前两张表各写一份、靠注释提醒人工同步,漏改即失联;新增命令只改这里。
// 动作名即 ini 键 ViewerShortcut/<动作名> 的后缀。
#include <QList>

struct ViewerHotkeyCmd {
    const char* name;    // 动作名(UTF-8;同时是 ini 键后缀与热键匹配串)
    const char* defKey;  // 默认快捷键;空串=默认不绑键(动作仍可从菜单/工具条走,设置页仍可自己绑)
};

inline const QList<ViewerHotkeyCmd>& viewerHotkeyCmds() {
    static const QList<ViewerHotkeyCmd> cmds = {
        { "下一个文件",  "Right"   },
        { "上一个文件",  "Left"    },
        { "放大",        "Ctrl+="  },
        { "缩小",        "Ctrl+-"  },
        { "适应窗口",    "F"       },
        { "1:1 像素",    ""        },   // #112:裸键 1 撤掉(用户不知道该键是干什么用的)
        { "播放/暂停",   "P"       },
    };
    return cmds;
}
