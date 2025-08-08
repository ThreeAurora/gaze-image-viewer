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
#include <QMetaType>
#include <QUrl>
#include <QDebug>
#include <QVector>

namespace Audiowave {

constexpr int kBuckets = 512;   // 波形横向分辨率(整条时间轴恒定 512 桶)

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
        m_totalUs = totalUs;
        m_peak.fill(0, kBuckets);
        m_trough.fill(0, kBuckets);
        m_filled = 0;
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
            emitSnapshot(gen, false);
        });
        connect(m_dec, &QAudioDecoder::errorOccurred, this, [this, gen](QAudioDecoder::Error) {
            if (gen != m_gen) return;
            qWarning() << "audiowave:" << (m_dec ? m_dec->errorString() : QString());
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
    }

signals:
    void snapshotReady(Audiowave::Snapshot snap);

private:
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
        int b0 = int((t0 * qint64(kBuckets)) / m_totalUs);
        int b1 = int(((t0 + span) * qint64(kBuckets)) / m_totalUs);
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
};

} // namespace Audiowave

Q_DECLARE_METATYPE(Audiowave::Snapshot)
