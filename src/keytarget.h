#pragma once
#include <QObject>
#include <QWidget>
#include <QDialog>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QMenu>
#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>

// 快捷键过滤器的共用判据:主窗口过滤器(装在 qApp 上,看得见所有控件的按键)
// 和对话框空格过滤器共用一份口径,免得两处对"这个键归谁"各说各话。

// 键盘对这些控件就是输入手段:字母、Space、Enter、Esc 都有本职,全局快捷键一律让路
inline bool textInputWidget(const QObject* o) {
    return qobject_cast<const QLineEdit*>(o)
        || qobject_cast<const QTextEdit*>(o)
        || qobject_cast<const QPlainTextEdit*>(o)
        || qobject_cast<const QAbstractSpinBox*>(o)
        || qobject_cast<const QComboBox*>(o)
        || qobject_cast<const QMenu*>(o);
}

// Space/Enter/Esc 在这些控件里有既定动作(激活按钮/选中或就地编辑/触发滑块),
// 但字母键不与它们冲突:F/D/F2 这类标记快捷键仍可全局生效
inline bool activationKeyWidget(const QObject* o) {
    return qobject_cast<const QAbstractButton*>(o)
        || qobject_cast<const QAbstractItemView*>(o)
        || qobject_cast<const QAbstractSlider*>(o);
}

// 收件人自身或某个祖先是对话框 → 整个对话框的键盘归它自己
inline bool insideDialog(const QObject* o) {
    for (const QWidget* w = qobject_cast<const QWidget*>(o); w; w = w->parentWidget())
        if (qobject_cast<const QDialog*>(w)) return true;
    return false;
}
