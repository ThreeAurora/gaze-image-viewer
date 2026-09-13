#pragma once
// 音频波形后端:QAudioDecoder 解码 + 512 桶 min/max 流式聚合,全部跑在专属
// QThread 上(Worker 被 moveToThread,解码器也在该线程创建)。主线程只经
// snapshotReady 收 ~1KB 快照 —— 性能红线(2026-09-01 用户令):波形可以晚出,
// 但绝不能拖累切文件/加载音频;与 QMediaPlayer 完全独立,解不出来只置
// failed,由 UI 降级成"波形不可用",播放不受任何影响。
//
// 内存与音频时长无关:固定 kBuckets×2 字节 qint8,1000 小时也是 1KB。
// 解到哪画到哪:100ms 节流发快照,filled 之前的桶渐进成形。
#include <QThread>
#include <QObject>
#include <QAudioDecoder>
#include <QAudioBuffer>
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QTimer>
#include <QMetaType>
#include <QProcess>
#include <QUrl>
#include <QDebug>
#include <QVector>

#include "../toolpath.h"
#include "../logger.h"

namespace Audiowave {

constexpr int kBuckets = 512;   // 波形横向分辨率(整条时间轴恒定 512 桶)
constexpr int kPcmRate = 4000;  // 兜底解码采样率:512 桶分辨率富余,数据量仅 8KB/s

struct Snapshot {
    QVector<qint8> peak;    // 每桶正峰 0..127
    QVector<qint8> trough;  // 每桶负谷 -127..0
    int  filled = 0;        // 已聚合到第几个桶(解到哪画到哪)
    bool failed = false;    // 解码器起不来/格式不支持 → UI 降级
};

class Worker : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;

public slots:
    // 换文件即调:start 自带代次重置,旧解码的一切残余回调都被闸掉
    void start(const QString& path, qint64 totalUs) {
        ++m_gen;
        const quint64 gen = m_gen;
        m_path = path;
        m_totalUs = totalUs;
        m_peak.fill(0, kBuckets);
        m_trough.fill(0, kBuckets);
        m_filled = 0;
        m_fbTries = 0;
        m_clock.restart();
        // 解码器销毁重建而非复用:setSource 换源后旧源的 bufferReady/finished
        // 残余事件仍可能压在队列里,逐个回调补代次判断不如重建干净
        if (m_dec) { m_dec->deleteLater(); m_dec = nullptr; }
        m_dec = new QAudioDecoder(this);
        connect(m_dec, &QAudioDecoder::bufferReady, this, [this, gen] {
            if (gen != m_gen || !m_dec) return;
            while (m_dec->bufferAvailable()) aggregate(m_dec->read());
            maybeEmit(gen);
        });
        connect(m_dec, &QAudioDecoder::durationChanged, this, [this, gen](qint64 ms) {
            if (gen != m_gen) return;
            if (ms > 0) m_totalUs = ms * 1000;   // QAudioDecoder 给毫秒
        });
        connect(m_dec, &QAudioDecoder::finished, this, [this, gen] {
            if (gen != m_gen) return;
            // FFmpeg 后端(Qt 6.8 起默认)的 QAudioDecoder 对带内嵌封面的 MP3
            // (mjpeg attached_pic,网易云下载件常态)会零 buffer 直接 finished。
            // 一桶未填就走 ffmpeg 管道兜底;error 同样兜(顺带扩格式覆盖)
            if (m_filled == 0) { tryFallback(gen); return; }
            emitSnapshot(gen, false);
        });
        // Qt 6.8 的 QAudioDecoder 保留 Qt5 信号名 error(Error),与同名 getter
        // error() 重载 → 取地址必须 QOverload 消歧
        connect(m_dec, QOverload<QAudioDecoder::Error>::of(&QAudioDecoder::error),
                this, [this, gen](QAudioDecoder::Error err) {
            if (gen != m_gen) return;
            qWarning() << "audiowave:" << err << (m_dec ? m_dec->errorString() : QString());
            if (m_filled == 0) { tryFallback(gen); return; }
            emitSnapshot(gen, true);
        });
        m_dec->setSource(QUrl::fromLocalFile(path));
        m_dec->start();
    }

    // 面板侧 QMediaPlayer durationChanged 的补发(毫秒→微秒由调用方换算);
    // QAudioDecoder 自己的 durationChanged 缺席时这里是桶映射的唯一分母来源
    void setTotalUs(qint64 us) { if (us > 0) m_totalUs = us; }

    // 离开音频模式/析构:代次自增闸掉一切在途回调,stop 停解码
    void cancel() {
        ++m_gen;
        if (m_dec) m_dec->stop();
        stopFallback();
    }

signals:
    void snapshotReady(Audiowave::Snapshot snap);

private:
    // ── ffmpeg 管道兜底 ──
    // 随包 ffmpeg 把音频解成单声道 s16le 裸 PCM 从 stdout 渐进吐出,聚合逻辑
    // 与 QAudioDecoder 路共用 aggregateSpan。仍在专属低优先级线程:readyRead
    // 驱动,主线程零参与(性能红线)。找不到 ffmpeg 返回 false(诚实降级)。
    // 兜底入口(解码器零产出时由 finished/error 调):先等桶映射分母 ——
    // QMediaPlayer 的 durationChanged 常晚于 QAudioDecoder 的 FINISHED
    // (媒体栈慢热),短重试等它,3.2s 仍无则诚实判"波形不可用"
    void tryFallback(quint64 gen) {
        if (m_ff) return;   // 兜底已在途(error/finished 双入口防重复)
        if (m_totalUs <= 0) {
            if (++m_fbTries > 8) {
                Logger::event("audiowave: 兜底放弃(3.2s 内总时长未知)");
                emitSnapshot(gen, true);
                return;
            }
            QTimer::singleShot(400, this, [this, gen] {
                if (gen == m_gen && m_filled == 0) tryFallback(gen);
            });
            return;
        }
        if (startFfmpegFallback(gen)) return;
        emitSnapshot(gen, true);
    }

    bool startFfmpegFallback(quint64 gen) {
        if (m_ff) return false;
        const QString exe = locateFfmpegTool(QStringLiteral("ffmpeg"));
        if (exe.isEmpty()) {
            Logger::event("audiowave: ffmpeg 兜底不可用(exe旁 ffmpeg/ 与 PATH 均无)");
            return false;
        }
        m_ffBytes = 0;
        m_ffTail.clear();
        m_ff = new QProcess(this);
        hideConsoleWindow(*m_ff);   // 控制台程序,不藏就闪黑窗
        m_ff->setProcessChannelMode(QProcess::ForwardedErrorChannel);
        connect(m_ff, &QProcess::readyReadStandardOutput, this, [this, gen] {
            if (gen != m_gen || !m_ff) return;
            // s16le 两字节一帧,而管道 chunk 尺寸任意:奇数尾字节缓存到下一块
            // 拼合。否则"整除丢弃 + 全量计字节"会让后续样本整体错位一帧,
            // 波形退化成满幅噪声
            QByteArray pcm = m_ffTail + m_ff->readAllStandardOutput();
            m_ffTail.clear();
            const int usable = pcm.size() & ~1;
            if (usable < pcm.size()) m_ffTail = pcm.mid(usable);
            if (usable < 2) return;
            // s16le 单声道:两字节一帧;起点按已消费字节推算
            const qint64 t0 = qint64(m_ffBytes) * 1000000 / kPcmRate;
            aggregatePcm(pcm.left(usable), t0, kPcmRate);
            m_ffBytes += usable;
            maybeEmit(gen);
        });
        connect(m_ff, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
                this, [this, gen](int code, QProcess::ExitStatus) {
            if (gen != m_gen) return;
            const bool hadStderr = !m_ffStderr.isEmpty();
            if (code != 0 || m_filled == 0)
                Logger::event(QStringLiteral(
                    "audiowave: ffmpeg 兜底退出 code=%1 filled=%2%3")
                        .arg(code).arg(m_filled)
                        .arg(hadStderr ? QStringLiteral(" stderr=") + m_ffStderr.left(200)
                                       : QString()));
            const bool ok = (code == 0 && m_filled > 0);
            if (ok)
                Logger::event(QStringLiteral("audiowave: 兜底完成 filled=%1/512").arg(m_filled));
            m_ff->deleteLater(); m_ff = nullptr;
            emitSnapshot(gen, !ok);
        });
        connect(m_ff, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
            m_ffStderr = QString::fromLocal8Bit(m_ff->readAllStandardError());
        });
        // 错误文本留给 finished 时的日志归因
        connect(m_ff, &QProcess::readyReadStandardError, this, [this] {
            m_ffStderr += QString::fromLocal8Bit(m_ff->readAllStandardError());
            if (m_ffStderr.size() > 4096) m_ffStderr.truncate(4096);
        });
        m_ff->start(exe, { QStringLiteral("-v"), QStringLiteral("error"),
                           QStringLiteral("-i"), m_path,
                           QStringLiteral("-vn"),           // 内嵌封面流正是病根,显式丢弃
                           QStringLiteral("-ac"), QStringLiteral("1"),
                           QStringLiteral("-ar"), QString::number(kPcmRate),
                           QStringLiteral("-f"), QStringLiteral("s16le"),
                           QStringLiteral("pipe:") });
        Logger::event(QStringLiteral(
            "audiowave: QAudioDecoder 零产出 → ffmpeg 管道兜底 '%1'").arg(m_path));
        return true;
    }

    void stopFallback() {
        if (!m_ff) return;
        QProcess* p = m_ff;
        m_ff = nullptr;          // 先摘引用:残余回调按空指针闸掉
        p->kill();
        p->waitForFinished(500);
        p->deleteLater();
    }

    // 裸 s16le PCM 聚合:chunk 覆盖 [t0, t0+字节换算的时长),极值摊进桶
    void aggregatePcm(const QByteArray& pcm, qint64 t0, int rate) {
        const int n = pcm.size() / 2;
        if (n <= 0) return;
        float mn = 0.f, mx = 0.f;
        const auto* s = reinterpret_cast<const qint16*>(pcm.constData());
        for (int i = 0; i < n; ++i) {
            const float v = s[i] / 32768.f;
            mn = qMin(mn, v); mx = qMax(mx, v);
        }
        aggregateSpan(mn, mx, t0, qint64(n) * 1000000 / rate);
    }

    void aggregateSpan(float mn, float mx, qint64 t0, qint64 spanUs) {
        if (m_totalUs <= 0) return;
        if (t0 < 0) return;
        int b0 = int((t0 * qint64(kBuckets)) / m_totalUs);
        int b1 = int(((t0 + spanUs) * qint64(kBuckets)) / m_totalUs);
        b0 = qBound(0, b0, kBuckets - 1);
        b1 = qBound(b0, b1, kBuckets - 1);
        const qint8 pk = qint8(qBound(-1.f, mx, 1.f) * 127.f);
        const qint8 tr = qint8(qBound(-1.f, mn, 1.f) * 127.f);
        for (int b = b0; b <= b1; ++b) {
            if (pk > m_peak[b])   m_peak[b] = pk;
            if (tr < m_trough[b]) m_trough[b] = tr;
        }
        if (b1 + 1 > m_filled) m_filled = b1 + 1;
    }

    void aggregate(const QAudioBuffer& buf) {
        if (m_totalUs <= 0) return;   // 总时长未知,桶映射无从谈起(等 durationChanged)
        const QAudioFormat fmt = buf.format();
        if (!fmt.isValid() || fmt.sampleRate() <= 0 || buf.frameCount() <= 0) return;
        const qint64 t0 = buf.startTime();
        if (t0 < 0) return;
        // 一次 buffer 通常 <10ms:取整个 buffer 的极值合并进覆盖到的桶。
        // 512 桶摊满整条时间轴,单 buffer 跨桶时的精度损失可忽略
        float mn = 0.f, mx = 0.f;
        const int n = buf.frameCount() * fmt.channelCount();
        switch (fmt.sampleFormat()) {
        case QAudioFormat::Int16: {
            const auto* s = buf.data<qint16>();
            for (int i = 0; i < n; ++i) { const float v = s[i] / 32768.f; mn = qMin(mn, v); mx = qMax(mx, v); }
            break; }
        case QAudioFormat::Int32: {
            const auto* s = buf.data<qint32>();
            for (int i = 0; i < n; ++i) { const float v = s[i] / 2147483648.f; mn = qMin(mn, v); mx = qMax(mx, v); }
            break; }
        case QAudioFormat::Float: {
            const auto* s = buf.data<float>();
            for (int i = 0; i < n; ++i) { mn = qMin(mn, s[i]); mx = qMax(mx, s[i]); }
            break; }
        case QAudioFormat::UInt8: {
            const auto* s = buf.data<uchar>();
            for (int i = 0; i < n; ++i) { const float v = (s[i] - 128) / 128.f; mn = qMin(mn, v); mx = qMax(mx, v); }
            break; }
        default:
            return;   // 其余样本格式不支持:波形留空,播放照常(诚实降级)
        }
        const qint64 span = qint64(buf.frameCount()) * 1000000 / fmt.sampleRate();
        aggregateSpan(mn, mx, t0, span);
    }

    void maybeEmit(quint64 gen) {
        if (m_clock.elapsed() < 100) return;   // 节流:渐进成形但不刷屏
        emitSnapshot(gen, false);
    }

    void emitSnapshot(quint64 gen, bool failed) {
        if (gen != m_gen) return;
        m_clock.restart();
        Snapshot s;
        s.peak = m_peak;
        s.trough = m_trough;
        s.filled = m_filled;
        s.failed = failed;
        emit snapshotReady(s);
    }

    QAudioDecoder* m_dec = nullptr;
    quint64 m_gen = 0;      // 代次:每次 start/cancel 自增,旧回调一律作废
    qint64  m_totalUs = 0;  // 桶映射分母(两路来源:自身 durationChanged / 面板补发)
    QVector<qint8> m_peak;
    QVector<qint8> m_trough;
    int m_filled = 0;
    QElapsedTimer m_clock;
    // ffmpeg 兜底态(仅 QAudioDecoder 零产出时在途)
    QProcess* m_ff = nullptr;
    qint64  m_ffBytes = 0;  // 已消费的 PCM 字节(推算当前 chunk 的时间起点)
    QByteArray m_ffTail;    // 管道 chunk 的奇数尾字节:缓存到下一块拼合
    QString m_ffStderr;     // ffmpeg 的 stderr,退出时归因用
    QString m_path;         // 当前解码文件(start 存,兜底管道的 -i 参数)
    int m_fbTries = 0;      // 兜底等分母的重试计数(start 清零)
};

} // namespace Audiowave

Q_DECLARE_METATYPE(Audiowave::Snapshot)
