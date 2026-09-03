#pragma once
// #214:删除/移动/改名前释放 Gaze 自己握着的文件句柄。Windows 上 QMediaPlayer(WMF)
// 打开播放中的视频/音频会锁住文件,Shell 删除与 QFile::rename 都会失败——用户令
// "播着视频删/移要自动解除其占用"。遍历顶层窗口找带 releaseFileLocks(QStringList)
// 的对象(MainWindow,Q_INVOKABLE 转调 PreviewPanel),经元对象调用免去头文件分层依赖。
// 找不到(面板未创建)=no-op。
#include <QStringList>
#include <QApplication>
#include <QWidget>
#include <QMetaObject>

inline void releaseGazeFileLocks(const QStringList& paths) {
    if (paths.isEmpty()) return;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || w->metaObject()->indexOfMethod("releaseFileLocks(QStringList)") < 0)
            continue;
        QMetaObject::invokeMethod(w, "releaseFileLocks",
                                  Qt::DirectConnection,
                                  Q_ARG(QStringList, paths));
    }
}
