#pragma once
// ═══════════════════════════════════════════════════════════════════════
// PreviewPanel 拆分后共享的文件级 helper。
// 原先是 previewpanel.cpp 里的 static 函数(内部链接,全文件一个实例);
// 现在多个 .cpp 编译单元都要用,故收在这里:
//   static -> inline + 具名 namespace pp_impl,保持「全程序一份定义」的原语义;
//   函数体一字未改,调用点只加 pp_impl:: 前缀。
// 仅供 previewpanel*.cpp 这几个编译单元 include,不给其他类使用。
// ═══════════════════════════════════════════════════════════════════════
#include "settings.h"
#include "theme.h"

#include <QColor>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QString>
#include <QPointer>

#include <atomic>
#include <mutex>
#include <vector>

namespace pp_impl {

// ═══ 媒体栈冷启动门闩(2026-09-07)═══
// FFmpeg 后端 66MB avcodec 首载是同步重活,GUI 线程上首建 QMediaPlayer 会
// 冻结界面 3~4 秒。启动链路里 dir-scan done 常在 show 后几十 ms 就兑现
// "上次选中/首项=视频",loadFile 抢在 warmUp 池线程载完 DLL 之前,GUI 就
// 被钉死——期间 PAINT 与 1.5s 超时兜底全堵在事件队列里发不出,即"任务栏
// 有图标窗口三秒不出"+灰块期的真身(09-07 日志实锤)。
// 门闩语义:warmUp 池线程载完 DLL 才置 ready;setupPlayer 见未就绪就把
// 本次装载挂起(记面板+路径),就绪后回 GUI 线程重放 loadFile。
inline std::atomic<bool>& mediaReadyFlag() {
    static std::atomic<bool> f{false};
    return f;
}
inline bool mediaStackReady() {
    return mediaReadyFlag().load(std::memory_order_acquire);
}
struct MediaLoadReq { QPointer<QObject> panel; QString path; };
inline std::mutex& mediaMx() { static std::mutex m; return m; }
inline std::vector<MediaLoadReq>& mediaPending() {
    static std::vector<MediaLoadReq> v;
    return v;
}
inline void deferMediaLoad(QObject* panel, const QString& path) {
    std::lock_guard<std::mutex> lk(mediaMx());
    mediaPending().push_back({panel, path});
}
inline std::vector<MediaLoadReq> takePendingMediaLoads() {
    std::lock_guard<std::mutex> lk(mediaMx());
    std::vector<MediaLoadReq> out;
    out.swap(mediaPending());
    return out;
}

// 标准图标染成白色(深色主题下 QStyle 图标是深色的)
inline QIcon whiteIcon(const QIcon& base, int size = 32) {
    QPixmap pm = base.pixmap(size, size);
    QPixmap white(pm.size());
    white.fill(Qt::transparent);
    QPainter p(&white);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(white.rect(), QColor("#FFFFFF"));
    p.end();
    return QIcon(white);
}

// 标准图标随主题染色:深色主题白、浅色主题黑(与全局文字色同向)。
// 浅色主题下播放控制栏底是浅灰,白色图标直接隐身 —— 控制栏一律走这版;
// 全屏浮层(恒黑底)仍用 whiteIcon。
inline QIcon themeIcon(const QIcon& base, int size = 32) {
    const QColor ink = QColor(QString::fromUtf8(
        Theme::light() ? "#1F1F26" : "#FFFFFF"));
    QPixmap pm = base.pixmap(size, size);
    QPixmap tinted(pm.size());
    tinted.fill(Qt::transparent);
    QPainter p(&tinted);
    p.drawPixmap(0, 0, pm);
    p.setCompositionMode(QPainter::CompositionMode_SourceIn);
    p.fillRect(tinted.rect(), ink);
    p.end();
    return QIcon(tinted);
}

// ═══════════════════════════════════════════
// 设置活接线:查看器/全屏页面的选项改动即时生效(无 need-restart)
//   读取一律走 AppSettings,不缓存 —— 热路径(逐帧 render)不碰 ini,
//   只在 fit/backdrop 这类低频时机取值
// ═══════════════════════════════════════════
inline bool s_bool(const QString& k, bool def) {
    return AppSettings::instance().get(k, def).toBool();
}
inline int s_int(const QString& k, int def) {
    return AppSettings::instance().get(k, def).toInt();
}

} // namespace pp_impl
