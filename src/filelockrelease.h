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
#include <QThread>

inline void releaseGazeFileLocks(const QStringList& paths) {
    if (paths.isEmpty()) return;
    for (QWidget* w : QApplication::topLevelWidgets()) {
        if (!w || w->metaObject()->indexOfMethod("releaseFileLocks(QStringList)") < 0)
            continue;
        QMetaObject::invokeMethod(w, "releaseFileLocks",
                                  Qt::DirectConnection,
                                  Q_ARG(QStringList, paths));
        // 悬停大小统计若正在递归扫描,握着目录树句柄也会顶住改名——一并请停
        if (w->metaObject()->indexOfMethod("abortGridDirSize()") >= 0)
            QMetaObject::invokeMethod(w, "abortGridDirSize",
                                      Qt::DirectConnection);
    }
}

// 改名重试:WMF 后端 teardown 后释放文件句柄是异步的,stop+deleteLater 返回
// 的瞬间句柄可能还在,首试 rename 会假失败;每 100ms 一次共 1 秒宽限
inline bool renameWithRetry(const QString& oldPath, const QString& newPath,
                            int tries = 10) {
    for (int i = 0; i < tries; ++i) {
        if (QFile::rename(oldPath, newPath)) return true;
        QThread::msleep(100);
    }
    return false;
}
