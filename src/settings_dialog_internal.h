#pragma once
// settings_dialog 拆分后各编译单元共用的内部 helper。
// 2026-09-01 结构性拆分:下面三个函数原本就是 settings_dialog.cpp 的文件级 static,
// 因为被多个新 .cpp 调用,原样搬进 namespace sd_impl 并把 static 改成 inline
// (pendingKeys 本来就是"返回函数局部 static 引用的访问器",单实例语义不变)。
// 只被单个编译单元用的 helper 不放这里,留在各自 .cpp 里。
#include <QWidget>
#include <QColor>
#include <QColorDialog>
#include <QSet>
#include <QString>
#include <QToolButton>
#include "i18n.h"

#include "settings.h"
#include "constants.h"

namespace sd_impl {

// ── 尚未接线的设置项 ──
// 2026-08-28 晚:51 项已全部接线,名单清空。保留机制本身 —— 以后新增设置项时,
// 先加进这里禁用,功能落地后再移出,保证 UI 上出现的每个选项都真实生效。
// 2026-08-30 复测过,当时名单确实该是空的(126 个键全有读点,查不到的 8 个是拼接键);
// 判据与复跑命令见 findings.md「设置键接线现状」。那条判据的边界也记在这:
// **有读点 ≠ 用得到** —— ViewerShortcut/* 当年就是这样过了判据却一条都不生效(见 #86)。
inline const QSet<QString>& pendingKeys() {
    // #122 起名单为空:General/exifDpi 与 dpiAdjust 已接回查看器
    // (oneToOneScale / pixelAspect,默认关=#95 的"真 1:1"),
    // Integration/iniLocation 的设置搬家也已实现(settings.cpp 引导+复制)。
    // 再出现"UI 上摆着但没接线"的键,先加进这里禁用,功能落地后移出。
    static const QSet<QString> keys;
    return keys;
}

inline void markPending(QWidget* w, const QString& key) {
    if (!w || !pendingKeys().contains(key)) return;
    w->setEnabled(false);
    w->setToolTip(gazeTr("尚未生效:该功能还没有实现,此项当前不影响程序行为"));
}

// 颜色选择:色块按钮 + 原生 QColorDialog(色相环/RGB/HTML 全功能色板,非文字选项)
inline QWidget* colorPick(const QString& key, const QString& def) {
    auto* btn = new QToolButton;
    btn->setFixedSize(48, 22);
    btn->setCursor(Qt::PointingHandCursor);
    auto apply = [btn](const QColor& c) {
        btn->setStyleSheet(QString("QToolButton{background:%1;border:1px solid %2;"
                                   "border-radius:3px;}").arg(c.name(), C_SEPARATOR));
    };
    apply(QColor(AppSettings::instance().get(key, def).toString()));
    QObject::connect(btn, &QToolButton::clicked, btn, [btn, key, def, apply]() {
        QColor cur(AppSettings::instance().get(key, def).toString());
        if (!cur.isValid()) cur = QColor(def);
        QColor c = QColorDialog::getColor(cur, btn, gazeTr("选择颜色"));
        if (!c.isValid()) return;
        AppSettings::instance().set(key, c.name(QColor::HexRgb));
        apply(c);
    });
    markPending(btn, key);
    return btn;
}

} // namespace sd_impl
