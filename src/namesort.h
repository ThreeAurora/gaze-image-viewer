#pragma once
// 名称"数字顺序"(1 < 2 < 10,资源管理器风格)的唯一实现。
// 网格排序 / 文件夹树 / 文件夹四合一 共用它:任何一处漏掉都会退回
// NTFS 原始扫描序(纯字典序:1, 10, 2),用户看到的正是这个。
// 依据实测(E:/dev/gaze, Qt 6.5.3):
//   QCollator(numericMode) → 1.jpg|2.jpg|10.jpg|img2|IMG3|img10|图片1|图片2|图片10 ✔
//   纯 QString::compare(忽略大小写) → 1.jpg|10.jpg|2.jpg ✘
#include <QCollator>
#include <QString>

inline int naturalNameCompare(const QString& a, const QString& b) {
    // QCollator 构造要取系统 locale,排序热路径上不能逐次新建;
    // thread_local:缩略图工作线程也会调,共享一个实例不安全
    static thread_local QCollator coll = [] {
        QCollator c;
        c.setNumericMode(true);
        c.setCaseSensitivity(Qt::CaseInsensitive);
        return c;
    }();
    return coll.compare(a, b);
}

// 等值时(如 "a1" 与 "a01")用区分大小写比较收尾,保证严格弱序且次序稳定
inline bool naturalNameLess(const QString& a, const QString& b) {
    const int r = naturalNameCompare(a, b);
    return r != 0 ? r < 0 : a < b;
}
