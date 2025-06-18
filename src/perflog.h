#pragma once
// ═══════════════════════════════════════════════════════════
// 轻量性能探针:只记录超过阈值的操作,追加到 exe目录/perf.log
// 用法:在函数开头构造 { PerfLog::Scope _t("名字", 阈值ms); }
// 析构时耗时超阈值才落盘——正常路径零刷屏,慢路径现形
// ═══════════════════════════════════════════════════════════

#include <QElapsedTimer>
#include <QFile>
#include <QDateTime>
#include <QCoreApplication>

namespace PerfLog {

inline QFile& file() {
    static QFile f(QCoreApplication::applicationDirPath() + "/perf.log");
    if (!f.isOpen())
        f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
    return f;
}

inline void write(const QString& line) {
    QFile& f = file();
    if (!f.isOpen()) return;
    const QByteArray out = QDateTime::currentDateTime()
        .toString("HH:mm:ss.zzz ").toUtf8() + line.toUtf8() + "\n";
    f.write(out);
    f.flush();
}

class Scope {
public:
    explicit Scope(const char* name, qint64 thresholdMs = 5)
        : m_name(name), m_threshold(thresholdMs) { m_timer.start(); }
    ~Scope() {
        const qint64 ms = m_timer.elapsed();
        if (ms >= m_threshold)
            write(QString("%1: %2 ms").arg(QLatin1String(m_name)).arg(ms));
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
private:
    const char* m_name;
    qint64 m_threshold;
    QElapsedTimer m_timer;
};

} // namespace PerfLog
