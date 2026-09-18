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
//
// ── 索引来源(2026-09-18 用户令新增)──
//   原设计只认自带实例,这对"已经装了 Everything"的用户是白费:那份全盘索引
//   早就在跑(服务/托盘常驻),Gaze 再起一个实例等于把几百万条记录的房间又盖
//   一间。现在按下面的顺序裁决索引来源:
//     ① 复用系统已装的 Everything(默认实例,es 不传 -instance)—— 用户若装了
//        且正在跑,直接连上去用他现成的索引,零新建、零额外内存;
//     ② 回退自带独立实例(-instance gaze)—— 没装系统版时的原路径;
//     ③ 两者都不可用 → 回退调用方内置 NTFS 索引(调用方原有逻辑,本模块只管
//        如实回报 false)。
//   ① 可由 Integration/reuseSystemEverything(默认开)关掉,关掉即回到原语义
//   "不探测不复用不干扰";②的部署与否由 deployed() 决定。
//   轻量版(不带 Everything.exe)就靠 ① 活着:没装系统版的用户落到 ③ 内置索引。
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
#include <QSettings>
#include <QMetaObject>
#include <functional>
#include <vector>
#include <algorithm>
#include "toolpath.h"
#include "settings.h"
#include "logger.h"

namespace ev_impl {

// 自带独立实例名。仅在"自行拉起实例 / 退出清理"时使用。
inline const QString& kInstanceName() {
    static const QString s = QStringLiteral("gaze");
    return s;
}

// ── 索引来源裁决(2026-09-18)──
// Bundled = 自带独立实例(-instance gaze),System = 复用用户已装的 Everything
enum class IndexSource { Bundled, System };

// 冷探测缓存:同一会话内只认真探一次(探测要起子进程,不能每次统计都探)。
// -1 = 未探过,0 = 系统版不可用,1 = 系统版可用。
inline int& systemProbeState() {
    static int v = -1;
    return v;
}

// 复用开关:Integration/reuseSystemEverything,默认开。
inline bool reuseSystemEnabled() {
    return AppSettings::instance()
               .get(QStringLiteral("Integration/reuseSystemEverything"), true).toBool();
}

// 用户手填的 Everything 位置(设置页 Integration/everythingPath;空=没填)。
// 也接受环境变量 GAZE_EVERYTHING(优先级更高,便于便携/脚本场景)。
// 缓存一份到内存:everythingExe() 是每次探测/每轮查询都要调的,别反复读 ini。
// invalidate != 0 时强制重读(设置页改完当场刷新,免得要重启)。
inline QString& manualEverythingExe(int invalidate = 0) {
    static QString v;
    static int loadedGen = -1;
    static int gen = 0;
    if (invalidate) ++gen;
    if (loadedGen != gen) {
        loadedGen = gen;
        v.clear();
        const QByteArray env = qgetenv("GAZE_EVERYTHING");
        if (!env.isEmpty())
            v = QString::fromLocal8Bit(env);
        else
            v = AppSettings::instance()
                    .get(QStringLiteral("Integration/everythingPath"), QString()).toString();
        v = QDir::toNativeSeparators(v.trimmed());
    }
    return v;
}

// 设置页改完路径后调用:下一个取用点重读,不重启也生效
inline void invalidateManualEverythingExe() {
    manualEverythingExe(/*invalidate*/ 1);
}

// 注册表 / 常见安装目录 / 运行中进程 三路巡查,找出用户装的 Everything.exe。
// 这是"复用"的兜底:es.exe 不传 -instance 已能连默认实例,不必知道路径;
// 但绿色版 / 装了没跑过 / 用户自己指定,都需要真把 exe 找出来。
inline QString probeEverythingExe() {
    // 结果缓存:巡查要读注册表 + 扫目录,不能每次统计都做一遍
    static bool known = false;
    static QString cached;
    if (known) return cached;
    known = true;

    // ① 注册表:Everything 官方安装时会写这几处(路径值多为带引号的完整 exe 路径)
    {
        const QStringList regPaths = {
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Everything"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Everything"),
            QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\Everything"),
            QStringLiteral("HKEY_LOCAL_MACHINE\\SOFTWARE\\Everything"),
            QStringLiteral("HKEY_CURRENT_USER\\SOFTWARE\\Everything"),
        };
        QSettings::setDefaultFormat(QSettings::NativeFormat);
        for (const QString& rp : regPaths) {
            QSettings rs(rp, QSettings::NativeFormat);
            for (const QString& k : { QStringLiteral("InstallLocation"),
                                      QStringLiteral("InstallPath"),
                                      QStringLiteral("InstallDir"),
                                      QStringLiteral("DisplayIcon") }) {
                QString v = rs.value(k).toString().trimmed();
                if (v.isEmpty()) continue;
                v.remove(QLatin1Char('"'));
                // DisplayIcon 可能带 ,0 之类后缀
                const int comma = v.indexOf(QLatin1Char(','));
                if (comma > 0) v = v.left(comma);
                QString cand = v;
                if (QFileInfo(cand).isDir())
                    cand = cand + QStringLiteral("/Everything.exe");
                else if (!cand.endsWith(QStringLiteral(".exe"), Qt::CaseInsensitive))
                    cand = cand + QStringLiteral("/Everything.exe");
                if (QFileInfo::exists(cand)) {
                    cached = QDir::toNativeSeparators(QFileInfo(cand).absoluteFilePath());
                    return cached;
                }
            }
        }
    }

    // ② 运行中进程的路径(用户实际在用的那份,最可信)。
    //    走 tasklist 的 /FO CSV 拿 PID 再不行 —— 直接用 wmic 太慢。
    //    这里用 PowerShell 的一句 Get-Process 取路径:只在探测阶段跑一次,可接受。
    {
        QProcess p;
        hideConsoleWindow(p);
        p.start(QStringLiteral("powershell"),
                { QStringLiteral("-NoProfile"), QStringLiteral("-NonInteractive"),
                  QStringLiteral("-Command"),
                  QStringLiteral("(Get-Process Everything -ErrorAction SilentlyContinue | "
                                 "Where-Object { $_.Path } | Select-Object -First 1).Path") },
                QIODevice::ReadOnly);
        if (p.waitForFinished(6000)) {
            const QString out = QString::fromLocal8Bit(p.readAllStandardOutput()).trimmed();
            if (!out.isEmpty() && QFileInfo::exists(out)) {
                cached = QDir::toNativeSeparators(out);
                return cached;
            }
        }
    }

    // ③ 常见安装目录(含各盘根、绿色版最爱放的几个位置)
    {
        QStringList cands;
        const QStringList drives = { QStringLiteral("C:"), QStringLiteral("D:"),
                                     QStringLiteral("E:"), QStringLiteral("F:") };
        for (const QString& d : drives) {
            cands << d + QStringLiteral("/Program Files/Everything/Everything.exe")
                  << d + QStringLiteral("/Program Files (x86)/Everything/Everything.exe")
                  << d + QStringLiteral("/Everything/Everything.exe");
        }
        const QString local = qEnvironmentVariable("LOCALAPPDATA");
        if (!local.isEmpty())
            cands << local + QStringLiteral("/Programs/Everything/Everything.exe");
        const QString found = locateFirstExisting(cands);
        if (!found.isEmpty()) { cached = found; return cached; }
    }

    return cached;   // 空 = 真没找到
}

// 用户可指定 Everything 本体位置的最终裁决:
//   环境变量/手填 > 自带目录 > 系统巡查
inline QString everythingExe() {
    const QString manual = manualEverythingExe();
    if (!manual.isEmpty() && QFileInfo::exists(manual)) return manual;
    const QString bundled = locateVendoredTool(QStringLiteral("everything"),
                                               QStringLiteral("Everything"));
    if (!bundled.isEmpty()) return bundled;
    return probeEverythingExe();
}

// es.exe 的定位:优先用 Everything 本体旁边那个(版本最匹配),再退回自带 / PATH
inline QString esExe() {
    const QString manual = manualEverythingExe();
    if (!manual.isEmpty()) {
        const QString beside = QFileInfo(manual).absolutePath() + QStringLiteral("/es.exe");
        if (QFileInfo::exists(beside)) return QDir::toNativeSeparators(beside);
    }
    return locateVendoredTool(QStringLiteral("everything"), QStringLiteral("es"));
}

// 跑一条 es 查询。-instance gaze 只在用自带实例时加。
inline QStringList esArgs(const QStringList& tail) {
    QStringList a;
    a << QStringLiteral("-instance") << kInstanceName();
    a << tail;
    return a;
}
inline QStringList esArgsFor(IndexSource src, const QStringList& tail) {
    if (src == IndexSource::System) return tail;   // 不传 -instance = 默认实例
    return esArgs(tail);
}


// 引擎是否"已部署"(文件在)。不涉及实例在不在跑 —— 探测实例是 instanceRunning()。
// 结果一旦为 false 就记住:exe 目录改名后重启 Gaze 才会再试,免得每个统计
// 都重复 stat。
// 注意(2026-09-18):只需 es.exe 就能复用系统版 Everything —— 轻量版正是只带
// es.exe。所以这里的判据是"es 在不在",Everything.exe(自建实例用)单独判。
inline bool esDeployed() {
    static bool known = false;
    static bool ok = false;
    if (!known) {
        const QString es = esExe();
        ok = !es.isEmpty() && QFileInfo::exists(es);
        known = true;
    }
    return ok;
}

// 自带 Everything 本体在不在(轻量版没有它 → 只能走"复用系统版"或内置索引)
inline bool bundledEverythingDeployed() {
    static bool known = false;
    static bool ok = false;
    if (!known) {
        const QString exe = everythingExe();
        ok = !exe.isEmpty() && QFileInfo::exists(exe);
        known = true;
    }
    return ok;
}

// 对外口径保持原样:有可用的 es 就算"已部署"(轻量版也算)。
// 调用方(mainwindow_nav.cpp 的 tryEverythingDirStat)拿 false 就直接走内置递归,
// 拿到 true 才挂异步查询 —— 查询内部再按实际来源裁决,失败仍会回退。
inline bool deployed() {
    return esDeployed();
}

// 同步跑一条 es 命令。只允许在子线程/进程退出收口调用,别在 GUI 线程等。
// 返回 0=成功(noResultError 时退出码 9 = 空结果集,视为成功),stdout 进 out。
inline int esSyncFor(IndexSource src, const QStringList& tail, QByteArray* out = nullptr,
                     bool noResultError = false, int timeoutMs = 15000) {
    const QString es = esExe();
    if (es.isEmpty()) return -2;
    QProcess p;
    hideConsoleWindow(p);
    p.start(es, esArgsFor(src, tail), QIODevice::ReadOnly);
    if (!p.waitForStarted(5000)) return -3;
    if (!p.waitForFinished(timeoutMs)) { p.kill(); p.waitForFinished(2000); return -4; }
    if (out) *out = p.readAllStandardOutput();
    const int code = p.exitCode();
    if (code != 0 && !(noResultError && code == 9)) return code;
    return 0;
}

// 兼容旧调用点(退出清理等固定用自带实例的场景)
inline int esSync(const QStringList& tail, QByteArray* out = nullptr,
                  bool noResultError = false, int timeoutMs = 15000) {
    return esSyncFor(IndexSource::Bundled, tail, out, noResultError, timeoutMs);
}

// 实例是否在跑(2026-09-09 修):必须用**走 IPC 真实往返**的查询探测。
// 旧实现 es -instance gaze -version 只打印 es.exe 自身版本(1.1.0.37),
// 不需要自带 Everything 实例存活也返回 0 → "永远就绪" → 引擎从没被拉起,
// 目录统计全走 IPC 全部 Error 8(日志:shutdown rc=8 / done ok=0)。
// 现在用 -get-result-count 空查询:空查询永远命中、返回总数,实例死活
// 决定成败 —— 没实例时 Everything IPC not found 退出非 0,毫秒即返。
inline bool instanceRunningFor(IndexSource src) {
    QByteArray out;
    return esSyncFor(src,
                     QStringList()
                       << QStringLiteral("-no-result-error")
                       << QStringLiteral("-get-result-count")
                       << QString(),
                     &out, /*noResultError*/ true, 5000) == 0;
}

// 兼容旧口径:自带实例
inline bool instanceRunning() {
    return instanceRunningFor(IndexSource::Bundled);
}

// 裁决索引来源(2026-09-18)。顺序:
//   ① 复用开关开着 且 有 es 且 系统默认实例在跑 → 复用系统版(只探一次,结果缓存)
//   ② 自带实例在跑 → 用自带的
//   ③ 自带 Everything 本体在 → 目前既没系统版也没起自带实例,交由 ensureRunning
//      去拉起自带实例(返回 Bundled,让它起)
//   ④ 都没有 → Bundled + ensureRunning 会失败,调用方回退内置索引
// 探测结果缓存:系统版"不在跑"也缓存住 —— 用户可能在 Gaze 开着时才退出
// Everything,那种边角情形不值得每次统计都付一次 5 秒探测的风险,重启 Gaze 即可。
inline IndexSource resolveSource() {
    if (reuseSystemEnabled() && esDeployed()) {
        if (systemProbeState() == -1) {
            const bool up = instanceRunningFor(IndexSource::System);
            systemProbeState() = up ? 1 : 0;
            if (up) Logger::event(QStringLiteral("everything engine: reuse system instance"));
            else    Logger::event(QStringLiteral("everything engine: system instance not found"));
        }
        if (systemProbeState() == 1) return IndexSource::System;
    }
    return IndexSource::Bundled;
}

// es 查询专用池:独占 1 线程,查询彼此串行,不挤压全局池(缩略图/缓存清扫)。
inline QThreadPool& esPool() {
    static QThreadPool pool;
    pool.setMaxThreadCount(1);
    return pool;
}

// 拉起/等待的会话级状态(提为具名 static:探测在 es 专用池跑,结果回 GUI
// 兑现,函数内局部 static 的写法撑不起这个拆分)
// src: 这一轮轮询要探哪个实例。复用系统版时**永远不会**走到轮询(它在裁决阶段
// 就已确认在跑),所以只有自带实例会进等待队列 —— 但字段留着,免得日后又踩"探
// 错了实例"的坑(2026-09-09 那次 es -version 假就绪正是这类错)。
struct EsWaiter {
    QPointer<QObject> p;
    std::function<void(bool)> cb;
    int left = 0;
    IndexSource src = IndexSource::Bundled;
};
inline std::vector<EsWaiter>& esWaiters() { static std::vector<EsWaiter> v; return v; }
inline bool& esEverReady() { static bool v = false; return v; }
inline bool& esLaunchTried() { static bool v = false; return v; }
inline bool& esLaunchOk() { static bool v = false; return v; }
inline QTimer*& esPoller() { static QTimer* v = nullptr; return v; }
// 轮询探哪个实例:等待队列里存的第一顺位的来源(同一时刻只会有一种在等)
inline IndexSource& esProbeSource() { static IndexSource v = IndexSource::Bundled; return v; }

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
    const IndexSource src = esProbeSource();
    esPool().start([src]() {
        const bool running = instanceRunningFor(src);
        QMetaObject::invokeMethod(qApp, [running]() { esSettle(running); },
                                  Qt::QueuedConnection);
    });
}

// 异步准备引擎,就绪后回调 done(true);ctx 悬空/超时(15s)/压根没部署 →
// done(false)。懒启动:已就绪的实例直接兑现,不重复拉起。
// 2026-09-18:先裁来源 —— 能复用系统版就直接兑现(它已在跑),不再自建实例。
inline void ensureRunning(QPointer<QObject> ctx, std::function<void(bool)> done) {
    // 已就绪快路径(2026-09-09):确认过一次实例在跑就不再反复 spawn 探测
    // 子进程 —— 引擎与 Gaze 生命周期绑定,起过且没退出就一路放行,直到
    // 本进程结束(shutdown 才停)
    if (esEverReady()) { if (done) done(true); return; }
    if (!esDeployed()) { if (done) done(false); return; }

    // ① 复用系统版:探测已在 resolveSource 里做过(带缓存),命中即当场兑现。
    // 注意顺序 —— 必须早于下面的"拉起自带实例":能复用时绝不起第二个索引。
    if (resolveSource() == IndexSource::System) {
        esEverReady() = true;
        if (done) done(true);
        return;
    }

    // ② 自带实例得靠本体才起得来。轻量版没有本体 —— 此时直接判失败,让调用方
    // 回退内置 NTFS 索引(而不是拿一个不存在的 exe 反复 startDetached)。
    if (!bundledEverythingDeployed()) { if (done) done(false); return; }

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

    esProbeSource() = IndexSource::Bundled;
    esWaiters().push_back({ctx, std::move(done), 30, IndexSource::Bundled});   // 30×500ms = 15s 上限

    if (!esPoller()) {
        esPoller() = new QTimer;   // 进程生命周期常驻,GUI 线程事件循环驱动
        esPoller()->setInterval(500);
        QObject::connect(esPoller(), &QTimer::timeout, []() { esProbeAsync(); });
    }
    if (!esPoller()->isActive()) esPoller()->start();
}

// 异步 es 查询:池线程里跑 esSync,结果经 QueuedConnection 回 ctx 所在线程。
// src 在进入时定死 —— 查询与"引擎已就绪"必须连同一个实例,不能中途改判。
inline void esRunAsync(const QStringList& tail, bool noResultError, int timeoutMs,
                       QPointer<QObject> ctx,
                       std::function<void(bool ok, const QByteArray& out)> done) {
    const IndexSource src = resolveSource();
    esPool().start([=]() {
        QByteArray out;
        const int rc = esSyncFor(src, tail, &out, noResultError, timeoutMs);
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
// 只停**自带实例**(固定 -instance gaze),用户机器上的系统版 Everything 绝不受
// 影响 —— 2026-09-18 加了复用之后这一条更要紧:复用系统版时如果这里少写
// -instance,es -exit 会把用户自己那套常驻索引连根停掉,那是不可接受的越界。
// 所以这里**不走 resolveSource()**,无条件用 Bundled 来源。
// 没起过自带实例时 es -exit 只是失败返回,无害。
inline void shutdown() {
    if (!esDeployed()) return;
    // 没起过自带实例就不必发这条:既省一次子进程,也避免任何"误停"的可能
    if (!esEverReady() && !esLaunchTried()) return;
    QByteArray out;
    const int rc = esSyncFor(IndexSource::Bundled, {QStringLiteral("-exit")},
                             &out, false, 3000);
    Logger::event(QStringLiteral("everything engine: shutdown rc=%1").arg(rc));
}

} // namespace ev_impl