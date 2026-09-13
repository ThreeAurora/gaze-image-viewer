#pragma once
// ═══════════════════════════════════════════════════════════
// Everything 引擎封装(#251,2026-09-07 用户令"轻量化集成 everything")
//
//   Gaze 捆绑 Everything 1.5 便携版(MIT,见 everything/License.txt)作文件索引
//   后端:以独立实例 -instance gaze 运行,与用户机器上已装的 Everything 完全
//   隔离(不探测不复用不干扰);查询走捆绑的 es.exe 命令行(IPC,毫秒级)。
//   本模块就绪后提供:
//     ① 文件夹大小瞬间统计  dirStatAsync —— 替代后台递归扫描(秒出精确值)
//     ② 全盘文件名快搜升级  searchFilesAsync —— Everything 语法原生支持
//     + 退出清理 shutdown(只停自带实例)
//   引擎懒启动:首次用到才 startDetached + 轮询就绪(15s 超时),Gaze 启动零开销。
//   一切失败路径都回退调用方内置引擎,本模块永不阻塞 GUI、永不崩。
//
//   回调约定:ctx 悬空(窗口销毁)则丢弃结果;回调一律经 QueuedConnection 回
//   GUI 线程。
// ═══════════════════════════════════════════════════════════
#include <QString>
#include <QStringList>
#include <QByteArray>
#include <QProcess>
#include <QTimer>
#include <QThreadPool>
#include <QPointer>
#include <QObject>
#include <QFileInfo>
#include <QDir>
#include <QMetaObject>
#include <functional>
#include <vector>
#include <algorithm>
#include "toolpath.h"
#include "logger.h"

namespace ev_impl {

// 独立实例名:所有 es 调用都前缀 -instance gaze,只连 Gaze 自带的实例。
// 用户系统里装着的 Everything(默认实例名 everything)与之互不干扰。
inline const QString& kInstanceName() {
    static const QString s = QStringLiteral("gaze");
    return s;
}

// 部署位置:exe 旁 everything/ 优先,PATH 兜底(与 ffmpeg/gs 同一套规约)
inline QString everythingExe() {
    return locateVendoredTool(QStringLiteral("everything"), QStringLiteral("Everything"));
}
inline QString esExe() {
    return locateVendoredTool(QStringLiteral("everything"), QStringLiteral("es"));
}

// 引擎是否"已部署"(文件在)。不涉及实例在不在跑 —— 探测实例是 instanceRunning()。
// 结果一旦为 false 就记住:exe 目录改名后重启 Gaze 才会再试,免得每个统计
// 都重复 stat。
inline bool deployed() {
    static bool known = false;
    static bool ok = false;
    if (!known) {
        ok = !esExe().isEmpty() && QFileInfo::exists(esExe())
          && !everythingExe().isEmpty() && QFileInfo::exists(everythingExe());
        known = true;
    }
    return ok;
}

inline QStringList esArgs(const QStringList& tail) {
    QStringList a;
    a << QStringLiteral("-instance") << kInstanceName();
    a << tail;
    return a;
}

// 同步跑一条 es 命令。只允许在子线程/进程退出收口调用,别在 GUI 线程等。
// 返回 0=成功(noResultError 时退出码 9 = 空结果集,视为成功),stdout 进 out。
inline int esSync(const QStringList& tail, QByteArray* out = nullptr,
                  bool noResultError = false, int timeoutMs = 15000) {
    const QString es = esExe();
    if (es.isEmpty()) return -2;
    QProcess p;
    hideConsoleWindow(p);
    p.start(es, esArgs(tail), QIODevice::ReadOnly);
    if (!p.waitForStarted(5000)) return -3;
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(2000); return -4; }
    if (out) *out = p.readAllStandardOutput();
    const int code = p.exitCode();
    if (code != 0 && !(noResultError && code == 9)) return code;
    return 0;
}

// 实例是否在跑(2026-09-09 修):必须用**走 IPC 真实往返**的查询探测。
// 旧实现 es -instance gaze -version 只打印 es.exe 自身版本(1.1.0.37),
// 不需要自带 Everything 实例存活也返回 0 → "永远就绪" → 引擎从没被拉起,
// 目录统计全走 IPC 全部 Error 8(日志:shutdown rc=8 / done ok=0)。
// 现在用 -get-result-count 空查询:空查询永远命中、返回总数,实例死活
// 决定成败 —— 没实例时 Everything IPC not found 退出非 0,毫秒即返。
inline bool instanceRunning() {
    QByteArray out;
    return esSync(QStringList()
                    << QStringLiteral("-no-result-error")
                    << QStringLiteral("-get-result-count")
                    << QString(), &out, /*noResultError*/ true, 5000) == 0;
}

// 拉起/等待的会话级状态(提为具名 static:探测在 es 专用池跑,结果回 GUI
// 兑现,函数内局部 static 的写法撑不起这个拆分)
struct EsWaiter { QPointer<QObject> p; std::function<void(bool)> cb; int left = 0; };
inline std::vector<EsWaiter>& esWaiters() { static std::vector<EsWaiter> v; return v; }
inline bool& esEverReady() { static bool v = false; return v; }
inline bool& esLaunchTried() { static bool v = false; return v; }
inline bool& esLaunchOk() { static bool v = false; return v; }
inline QTimer*& esPoller() { static QTimer* v = nullptr; return v; }

// 兑现/淘汰等待者(必须 GUI 线程;done 回调约定同原实现)
inline void esSettle(bool running) {
    if (running) esEverReady() = true;
    bool anyAlive = false;
    for (auto& w : esWaiters()) {
        if (w.left > 0) --w.left;
        if (running || w.left == 0) {
            // 兑现(不管 ctx 在不在):结果丢了就丢了,窗口走了不拦引擎
            if (w.p && w.cb) w.cb(running);
            w.cb = nullptr;
        } else {
            anyAlive = true;
        }
    }
    esWaiters().erase(std::remove_if(esWaiters().begin(), esWaiters().end(),
                        [](const EsWaiter& w) { return !w.cb; }),
                      esWaiters().end());
    if (!anyAlive && esPoller()) esPoller()->stop();
}

// 实例探测跑进 es 专用池:instanceRunning 是同步等子进程(上限 5s),在 GUI
// 线程跑一次就是一次整窗冻结(引擎冷启动被杀软拦时踩实)——与"永不阻塞
// GUI"的模块承诺相悖。结果经队列回 GUI 再兑现
inline void esProbeAsync() {
    esPool().start([]() {
        const bool running = instanceRunning();
        QMetaObject::invokeMethod(qApp, [running]() { esSettle(running); },
                                  Qt::QueuedConnection);
    });
}

// 异步拉起自带 Everything 实例,就绪后回调 done(true);ctx 悬空/超时(15s)/
// 压根没部署 → done(false)。懒启动:已就绪的实例直接兑现,不重复拉起。
inline void ensureRunning(QPointer<QObject> ctx, std::function<void(bool)> done) {
    // 已就绪快路径(2026-09-09):确认过一次实例在跑就不再反复 spawn 探测
    // 子进程 —— 引擎与 Gaze 生命周期绑定,起过且没退出就一路放行,直到
    // 本进程结束(shutdown 才停)
    if (esEverReady()) { if (done) done(true); return; }
    if (!deployed()) { if (done) done(false); return; }

    // 只尝试 startDetached 一次:再失败说明启动必然给不出引擎(缺 VC 运行库等),
    // 后续调用方直接走内置引擎回退,别拿 es 进程循环砸实例。
    if (!esLaunchTried()) {
        esLaunchTried() = true;
        const QString wd = QFileInfo(everythingExe()).absolutePath();
        esLaunchOk() = QProcess::startDetached(
            everythingExe(),
            {QStringLiteral("-instance"), kInstanceName(), QStringLiteral("-startup")},
            wd);
        if (!esLaunchOk()) { if (done) done(false); return; }
        Logger::event(QStringLiteral("everything engine: start detached (instance gaze)"));
    }
    if (!esLaunchOk()) { if (done) done(false); return; }

    esWaiters().push_back({ctx, std::move(done), 30});   // 30×500ms = 15s 上限

    if (!esPoller()) {
        esPoller() = new QTimer;   // 进程生命周期常驻,GUI 线程事件循环驱动
        esPoller()->setInterval(500);
        QObject::connect(esPoller(), &QTimer::timeout, []() { esProbeAsync(); });
    }
    if (!esPoller()->isActive()) esPoller()->start();
}

// es 查询专用池:独占 1 线程,查询彼此串行,不挤压全局池(缩略图/缓存清扫)。
inline QThreadPool& esPool() {
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    return pool;
}

// 异步 es 查询:池线程里跑 esSync,结果经 QueuedConnection 回 ctx 所在线程。
inline void esRunAsync(const QStringList& tail, bool noResultError, int timeoutMs,
                       QPointer<QObject> ctx,
                       std::function<void(bool ok, const QByteArray& out)> done) {
    esPool().start([=]() {
        QByteArray out;
        const int rc = esSync(tail, &out, noResultError, timeoutMs);
        QMetaObject::invokeMethod(ctx, [=]() {
            if (done) done(rc == 0, out);
        }, Qt::QueuedConnection);
    });
}

// ── ① 文件夹大小瞬间统计 ──
// 递归总体积:es -a-d(path 下全部文件) -get-total-size(Everything 1.5 才有,
// 输出字节数)。空目录=0 字节,不算失败。ok=false → 调用方回退内置递归扫描。
inline void dirStatAsync(const QString& dirPath, QPointer<QObject> ctx,
                         std::function<void(bool ok, qint64 bytes)> done) {
    QStringList tail;
    tail << QStringLiteral("-no-result-error")
         << QStringLiteral("-a-d")
         << QStringLiteral("-path") << dirPath
         << QStringLiteral("-get-total-size");
    esRunAsync(tail, /*noResultError*/ true, 15000, ctx, [=](bool ok, const QByteArray& out) {
        qint64 bytes = 0;
        if (ok) bytes = out.trimmed().toLongLong();
        // 空目录(0 字节)也是有效结果
        if (done) done(ok, bytes);
    });
}

// ── ② 全盘文件名快搜 ──
// query 用 Everything 原生语法(空格=AND、| OR、! NOT、ext:/size:/dm:、
// 通配符、-r 正则)。stdout 每行一个完整路径,截到 limit 条。
// ok=false → 调用方回退内置 USN 索引引擎。
inline void searchFilesAsync(const QString& query, int limit, QPointer<QObject> ctx,
                             std::function<void(bool ok, const QStringList& paths)> done) {
    QStringList tail;
    tail << QStringLiteral("-no-result-error")
         << QStringLiteral("-a-d")
         << QStringLiteral("-n") << QString::number(limit)
         << query;
    esRunAsync(tail, /*noResultError*/ true, 10000, ctx, [=](bool ok, const QByteArray& out) {
        QStringList paths;
        if (ok) {
            const QString text = QString::fromUtf8(out);
            const auto lines = text.split(QLatin1Char('\n'));
            for (const QString& line : lines) {
                const QString p = line.trimmed();
                if (!p.isEmpty()) paths << p;
            }
        }
        if (done) done(ok, paths);
    });
}

// 命中总数(快搜状态栏显示"共命中 N 项"):
inline void countAsync(const QString& query, QPointer<QObject> ctx,
                       std::function<void(bool ok, qint64 count)> done) {
    QStringList tail;
    tail << QStringLiteral("-no-result-error")
         << QStringLiteral("-a-d")
         << query
         << QStringLiteral("-get-result-count");
    esRunAsync(tail, /*noResultError*/ true, 10000, ctx, [=](bool ok, const QByteArray& out) {
        qint64 n = 0;
        if (ok) n = out.trimmed().toLongLong();
        if (done) done(ok, n);
    });
}

// ── 退出清理 ──
// 只停自带实例(-instance gaze 已内置于 esArgs),用户机器上的系统版 Everything
// 不受影响。没有任何实例时 es -exit 只是失败返回,无害。
inline void shutdown() {
    if (!deployed()) return;
    QByteArray out;
    const int rc = esSync({QStringLiteral("-exit")}, &out, false, 3000);
    Logger::event(QStringLiteral("everything engine: shutdown rc=%1").arg(rc));
}

} // namespace ev_impl