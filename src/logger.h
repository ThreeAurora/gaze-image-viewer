#pragma once
// ═══════════════════════════════════════════════════════════
// gaze.log 事件日志:诊断"卡死/崩溃"类问题(perf.log 记慢,这里记"死")
//   · event()      任意线程安全落盘,业务路径埋检查点用
//   · 消息处理器    捕获 Qt/媒体后端的 qWarning/qCritical(WMF 报错会现形)
//   · touch()      GUI 心跳;看门狗线程发现 >12s 无心跳 → 进程活着但 GUI 卡死
//   · SEH 过滤器    崩溃时先写 FATAL 行,再存 gaze_crash.dmp 迷你转储
// 判读:gaze.log 末尾是 WATCHDOG → 卡死;是 FATAL(+dmp) → 崩溃;
//       都没有戛然而止 → 硬崩(看最后一个检查点定位到哪一步)
// ═══════════════════════════════════════════════════════════

#include <QDateTime>
#include <QFile>
#include <QDir>
#include <QMutex>
#include <QMutexLocker>
#include <QCoreApplication>
#include <QThread>
#include <atomic>
#include <thread>
#include <chrono>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>
#endif

namespace Logger {

constexpr int kRotateBytes = 2 * 1024 * 1024;

inline QString path() {
    return QCoreApplication::applicationDirPath() + QStringLiteral("/gaze.log");
}

inline QFile& file() {
    static QFile f(path());
    if (!f.isOpen())
        f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    return f;
}

inline QMutex& mutex() { static QMutex m; return m; }

inline void event(const QString& msg) {
    const QString line = QDateTime::currentDateTime().toString("HH:mm:ss.zzz ")
        + QStringLiteral("[") + QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()))
        + QStringLiteral("] ") + msg + QStringLiteral("\n");
    const QByteArray out = line.toUtf8();
    QMutexLocker lk(&mutex());
    QFile& f = file();
    if (!f.isOpen()) return;
    f.write(out);
    f.flush();
}

// ── 冷启动分段计时 ──
// age = 进程创建至今的墙钟毫秒(GetProcessTimes),CRT/ DLL 加载 / QApplication 构造
// 全算在内 —— 从 main() 里起表测不到那一段,而"双击到出图"用户是连那段一起等的。
// dt  = 距上一次 boot() 的毫秒,直接读出哪一段吃掉了启动时间。
// 只在启动路径调用(一次十几行),不进逐条目热路径。
inline qint64 processAgeMs() {
#ifdef _WIN32
    FILETIME c = {}, e = {}, k = {}, u = {};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1;
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME& t) {
        return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    return qint64((toU64(now) - toU64(c)) / 10000ull);
#else
    return -1;
#endif
}

inline void boot(const char* phase) {
    static qint64 prevAge = 0;
    const qint64 age = processAgeMs();
    event(QStringLiteral("startup %1 age=%2ms dt=%3ms")
              .arg(QLatin1String(phase)).arg(age).arg(prevAge ? age - prevAge : age));
    prevAge = age;
}

// ── 冷启动分段计时 ──
// age = 进程创建至今的墙钟毫秒(GetProcessTimes),CRT/ DLL 加载 / QApplication 构造
// 全算在内 —— 从 main() 里起表测不到那一段,而"双击到出图"用户是连那段一起等的。
// dt  = 距上一次 boot() 的毫秒,直接读出哪一段吃掉了启动时间。
// 只在启动路径调用(一次十几行),不进逐条目热路径。
inline qint64 processAgeMs() {
#ifdef _WIN32
    FILETIME c = {}, e = {}, k = {}, u = {};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1;
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME& t) {
        return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    return qint64((toU64(now) - toU64(c)) / 10000ull);
#else
    return -1;
#endif
}

inline void boot(const char* phase) {
    static qint64 prevAge = 0;
    const qint64 age = processAgeMs();
    event(QStringLiteral("startup %1 age=%2ms dt=%3ms")
              .arg(QLatin1String(phase)).arg(age).arg(prevAge ? age - prevAge : age));
    prevAge = age;
}

// ── 冷启动分段计时 ──
// age = 进程创建至今的墙钟毫秒(GetProcessTimes),CRT/ DLL 加载 / QApplication 构造
// 全算在内 —— 从 main() 里起表测不到那一段,而"双击到出图"用户是连那段一起等的。
// dt  = 距上一次 boot() 的毫秒,直接读出哪一段吃掉了启动时间。
// 只在启动路径调用(一次十几行),不进逐条目热路径。
inline qint64 processAgeMs() {
#ifdef _WIN32
    FILETIME c = {}, e = {}, k = {}, u = {};
    if (!GetProcessTimes(GetCurrentProcess(), &c, &e, &k, &u)) return -1;
    FILETIME now = {};
    GetSystemTimeAsFileTime(&now);
    auto toU64 = [](const FILETIME& t) {
        return (quint64(t.dwHighDateTime) << 32) | t.dwLowDateTime;
    };
    return qint64((toU64(now) - toU64(c)) / 10000ull);
#else
    return -1;
#endif
}

inline void boot(const QString& phase) {
    static qint64 prevAge = 0;
    const qint64 age = processAgeMs();
    event(QStringLiteral("startup %1 age=%2ms dt=%3ms")
              .arg(phase).arg(age).arg(prevAge ? age - prevAge : age));
    prevAge = age;
}

// ── Qt 消息分流:qWarning/qCritical(含媒体后端报错)一并落盘 ──
inline void msgHandler(QtMsgType type, const QMessageLogContext& ctx, const QString& msg) {
    const char* tag = "DBG";
    switch (type) {
        case QtDebugMsg:    tag = "DBG";   break;
        case QtInfoMsg:     tag = "INF";   break;
        case QtWarningMsg:  tag = "WRN";   break;
        case QtCriticalMsg: tag = "ERR";   break;
        case QtFatalMsg:    tag = "FATAL"; break;
    }
    QString line = QString::fromLatin1(tag) + ": " + msg;
    if (ctx.category && *ctx.category)
        line = "[" + QString::fromLatin1(ctx.category) + "] " + line;
    event(line);
}

// ── GUI 心跳 ──
inline std::atomic<long long>& guiTick() {
    static std::atomic<long long> t{0};
    return t;
}
inline void touch() { guiTick().store(QDateTime::currentMSecsSinceEpoch()); }

inline void startWatchdog() {
    std::thread([]() {
        long long lastStallLog = 0;
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            const long long now = QDateTime::currentMSecsSinceEpoch();
            const long long stall = now - guiTick().load();
            if (stall > 12000 && now - lastStallLog >= 30000) {
                event(QStringLiteral("WATCHDOG: GUI 线程 %1 秒无心跳 — 进程仍在,判定卡死(非崩溃)")
                          .arg(stall / 1000));
                lastStallLog = now;
            }
        }
    }).detach();
}

#ifdef _WIN32
// 崩溃路径不用 Qt/堆:纯 WinAPI 追加 FATAL 行 + 存 minidump
inline LONG WINAPI sehFilter(EXCEPTION_POINTERS* ep) {
    const QString logDir = QCoreApplication::applicationDirPath();
    const std::wstring logW = (logDir + QStringLiteral("/gaze.log")).toStdWString();
    const std::wstring dmpW = (logDir + QStringLiteral("/gaze_crash.dmp")).toStdWString();

    wchar_t buf[192];
    _snwprintf(buf, 192, L"FATAL exception code=0x%08lX addr=%p tid=%lu\r\n",
               ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0,
               ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionAddress : nullptr,
               GetCurrentThreadId());
    HANDLE lf = CreateFileW(logW.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lf != INVALID_HANDLE_VALUE) {
        DWORD written = 0;
        WriteFile(lf, buf, static_cast<DWORD>(wcslen(buf) * sizeof(wchar_t)), &written, nullptr);
        CloseHandle(lf);
    }

    HMODULE dbg = LoadLibraryW(L"dbghelp.dll");
    if (dbg) {
        using Fn = BOOL(WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                 PMINIDUMP_EXCEPTION_INFORMATION,
                                 PMINIDUMP_USER_STREAM_INFORMATION,
                                 PMINIDUMP_CALLBACK_INFORMATION);
        auto dump = reinterpret_cast<Fn>(GetProcAddress(dbg, "MiniDumpWriteDump"));
        HANDLE df = CreateFileW(dmpW.c_str(), GENERIC_WRITE, 0, nullptr,
                                CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (df != INVALID_HANDLE_VALUE && dump) {
            MINIDUMP_EXCEPTION_INFORMATION mei{GetCurrentThreadId(), ep, FALSE};
            dump(GetCurrentProcess(), GetCurrentProcessId(), df,
                 MiniDumpNormal, &mei, nullptr, nullptr);
        }
        if (df != INVALID_HANDLE_VALUE) CloseHandle(df);
    }
    return EXCEPTION_CONTINUE_SEARCH;   // 交还系统(WER 照常弹)
}
#endif

inline void init() {
    // 先轮转再打开:>2MB 的旧日志挪到 gaze.log.old(只留一代)
    {
        QFile old(path());
        if (old.exists() && old.size() > kRotateBytes) {
            const QString aged = path() + ".old";
            QFile::remove(aged);
            old.rename(aged);
        }
    }
    guiTick().store(QDateTime::currentMSecsSinceEpoch());
    event(QStringLiteral("── session start pid=%1 qt=%2 exe=%3")
              .arg(qApp->applicationPid())
              .arg(QLatin1String(qVersion()))
              .arg(QCoreApplication::applicationFilePath()));
    boot("pre-main");   // 进程创建 → 这里:DLL 加载 + Qt 插件探测 + QApplication 构造
    qInstallMessageHandler(msgHandler);
#ifdef _WIN32
    SetUnhandledExceptionFilter(sehFilter);
#endif
    startWatchdog();
}

} // namespace Logger
