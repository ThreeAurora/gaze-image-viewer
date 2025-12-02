#include "thumbnailer.h"
#include "constants.h"
#include "namesort.h"
#include "settings.h"
#include "imgproc.h"
#include "wicdecode.h"
#include "logger.h"

#include <QFileInfo>
#include <QImage>
#include <QThread>
#include <QPainter>
#include <QPainterPath>
#include <QSqlQuery>
#include <QSqlError>
#include <QDir>
#include <QFile>
#include <QBuffer>
#include <QProcess>
#include <QTemporaryFile>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QHash>
#include <QMutex>
#include <QCryptographicHash>
#include <QPixmap>
#include <QImageReader>
#include <algorithm>   // 必须在 windows.h 之前:min/max 宏会咬坏 libstdc++ 头

#include <windows.h>
#include <shobjidl.h>
#include <shlguid.h>

// ── 前置声明 ──
static QImage windowsShellThumb(const QString& filePath, int size);
#include <wincodec.h>
#include <QDateTime>
#include <QVariant>
#include <QImageReader>
#include <QBuffer>
#include <QFile>
#include <chrono>
#include <cstring>
#include <cmath>

#ifdef HAS_FFMPEG
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}
#endif

// ── 本编译单元(#129 从 thumbnailer.cpp 拆出):视频缩略图:ffmpeg C API / QProcess 回退(含 #121 HDR tonemap) / 四帧拼图 ──

// ═══════════════════════════════════════════
// 视频四帧拼图(Thumbs/video4)
//   从 Thumbs/videoFramePct 指定的位置起,在剩余时长内均匀取 4 帧
// ═══════════════════════════════════════════
QImage Thumbnailer::videoContactSheet(const QString& filePath, int size) {
    const int start = prefs().framePct;
    const int gap = 2;
    const int cell = (size - gap) / 2;
    QImage sheet(size, size, QImage::Format_RGB32);
    sheet.fill(0xFF000000);
    QPainter pt(&sheet);
    int drawn = 0;
    for (int i = 0; i < 4; ++i) {
        const int pct = start + (100 - start) * i / 4;
        QImage f = videoThumbFFmpeg(filePath, cell, pct);
        if (f.isNull()) continue;
        f = f.scaled(cell, cell, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        const int ox = (i % 2) * (cell + gap);
        const int oy = (i / 2) * (cell + gap);
        pt.drawImage(ox, oy, f.copy(0, 0, qMin(cell, f.width()), qMin(cell, f.height())));
        ++drawn;
    }
    pt.end();
    return drawn ? postProcess(sheet, size) : QImage();
}

// ═══════════════════════════════════════════
// 视频缩略图 — 外部 ffmpeg 定位(#112)
//   以前直接 proc.start("ffmpeg") 走 PATH:机器上没装就静默降级成 Windows Shell
//   缩略图,用户看不出区别也不知道为什么。定位顺序照 #110 的 Ghostscript ——
//   随 gaze 走的那份永远优先,PATH 只是兜底,两处都没有才降级且必留一条日志痕。
//   定位函数收口在 toolpath.h(#116 起静图解码共用同一份)。
// ═══════════════════════════════════════════
namespace {

QString ffmpegExe() {
    static const QString exe = locateFfmpegTool(QStringLiteral("ffmpeg"));
    if (exe.isEmpty()) {
        // 只报一次:这是逐条目热路径,每条都写日志就成了新的性能问题
        static const bool warned = [] {
            Logger::event(QStringLiteral(
                "THUMB ffmpeg 未找到(exe旁 ffmpeg/ 与 PATH 均无)→ 视频缩略图降级为 Shell 缩略图"));
            return true;
        }();
        Q_UNUSED(warned);
    }
    return exe;
}

// 容器时长(秒),取不到返回 -1。只在需要按百分比取帧时才调用
double probeDurationSec(const QString& filePath) {
    static QMutex        mtx;
    static QHash<QString, double> memo;
    {
        QMutexLocker lk(&mtx);
        const auto it = memo.constFind(filePath);
        if (it != memo.constEnd()) return it.value();
    }

    double dur = -1.0;
    const QString exe = locateFfmpegTool(QStringLiteral("ffprobe"));
    if (!exe.isEmpty()) {
        QProcess p;
        hideConsoleWindow(p);   // ffprobe 是控制台程序:不隐藏的话每个视频闪一次黑窗
        p.setProcessChannelMode(QProcess::MergedChannels);
        p.start(exe, { QStringLiteral("-v"), QStringLiteral("error"),
                       QStringLiteral("-show_entries"), QStringLiteral("format=duration"),
                       QStringLiteral("-of"),
                       QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                       filePath });
        if (p.waitForFinished(3000)) {
            for (const QString& ln : QString::fromUtf8(p.readAllStandardOutput()).split('\n')) {
                bool ok = false;
                const double v = ln.trimmed().toDouble(&ok);
                if (ok && v > 0) { dur = v; break; }
            }
        } else {
            p.kill();
            p.waitForFinished(200);
        }
    }
    {
        QMutexLocker lk(&mtx);
        if (memo.size() > 8192) memo.clear();   // 上千条也就几百 KB,到顶整体重来
        memo.insert(filePath, dur);
    }
    return dur;
}

// Thumbs/videoFramePct:0=默认取第 1 秒(原行为),>0=取全长的百分比处。
// 只有非默认值才付 ffprobe 这笔开销(实测单次约 110ms),默认路径一次都不探。
int seekMsForPct(const QString& filePath, int pct, double knownDurSec = -1.0) {
    if (pct <= 0) return 1000;
    double dur = knownDurSec;
    if (dur <= 0) dur = probeDurationSec(filePath);
    if (dur <= 0) return 1000;   // 探不到时长就退回默认位置,不瞎猜
    const qint64 ms = qint64(dur * 1000.0) * pct / 100;
    return int(qBound(qint64(0), ms, qint64(dur * 1000.0) - 1));
}

} // namespace

// ═══════════════════════════════════════════
// 视频缩略图 — FFmpeg C API（优先）
// ═══════════════════════════════════════════
QImage Thumbnailer::videoThumbFFmpeg(const QString& filePath, int size, int pctOverride) {
    // 取帧位置 Thumbs/videoFramePct:0=默认取第 1 秒,>0=取全长的百分比处
    // pctOverride >= 0:四帧拼图按各自位置取帧,不使用全局设置。
    // 两条管线(内置 libavcodec / 外部 ffmpeg.exe)共用这一个换算:
    // 以前 pctOverride 到了 #else 分支就被丢掉,四帧拼图于是把同一帧画满四格
    const int pct = pctOverride >= 0 ? pctOverride : prefs().framePct;
#ifdef HAS_FFMPEG
    AVFormatContext* fmtCtx = nullptr;
    if (avformat_open_input(&fmtCtx, filePath.toUtf8().constData(), nullptr, nullptr) < 0)
        return videoThumbFallback(filePath, size, pct);

    if (avformat_find_stream_info(fmtCtx, nullptr) < 0) {
        avformat_close_input(&fmtCtx);
        return videoThumbFallback(filePath, size, pct);
    }

    int videoStream = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (videoStream < 0) {
        avformat_close_input(&fmtCtx);
        return videoThumbFallback(filePath, size, pct);
    }

    AVCodecParameters* codecPar = fmtCtx->streams[videoStream]->codecpar;
    const AVCodec* codec = avcodec_find_decoder(codecPar->codec_id);
    if (!codec) {
        avformat_close_input(&fmtCtx);
        return videoThumbFallback(filePath, size, pct);
    }

    AVCodecContext* codecCtx = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(codecCtx, codecPar);
    if (avcodec_open2(codecCtx, codec, nullptr) < 0) {
        avcodec_free_context(&codecCtx);
        avformat_close_input(&fmtCtx);
        return videoThumbFallback(filePath, size, pct);
    }

    // 容器自己报了时长就不必再叫 ffprobe 探一次(缩略图取帧位置不是精度活)
    const double durSec = fmtCtx->duration > 0
                        ? static_cast<double>(fmtCtx->duration) / AV_TIME_BASE : -1.0;
    const int64_t posUs = static_cast<int64_t>(seekMsForPct(filePath, pct, durSec)) * 1000;
    int64_t seekTarget = av_rescale_q(posUs, AV_TIME_BASE_Q,
                                      fmtCtx->streams[videoStream]->time_base);
    av_seek_frame(fmtCtx, videoStream, seekTarget, AVSEEK_FLAG_BACKWARD);

    // 清空解码缓存
    avcodec_flush_buffers(codecCtx);

    AVFrame* frame = av_frame_alloc();
    AVPacket* pkt = av_packet_alloc();
    bool gotFrame = false;

    while (av_read_frame(fmtCtx, pkt) >= 0) {
        if (pkt->stream_index == videoStream) {
            if (avcodec_send_packet(codecCtx, pkt) >= 0) {
                int ret = avcodec_receive_frame(codecCtx, frame);
                if (ret == 0) { gotFrame = true; av_packet_unref(pkt); break; }
            }
        }
        av_packet_unref(pkt);
    }

    QImage result;
    if (gotFrame) {
        int srcW = frame->width;
        int srcH = frame->height;
        AVPixelFormat srcFmt = static_cast<AVPixelFormat>(frame->format);

        double aspect = static_cast<double>(srcW) / srcH;
        int dw, dh;
        if (aspect > 1.0) { dw = size; dh = static_cast<int>(size / aspect); }
        else              { dh = size; dw = static_cast<int>(size * aspect); }
        if (dw < 1) dw = 1;
        if (dh < 1) dh = 1;

        SwsContext* sws = sws_getContext(
            srcW, srcH, srcFmt,
            dw, dh, AV_PIX_FMT_RGB32,
            SWS_BILINEAR, nullptr, nullptr, nullptr);

        if (sws) {
            int rgbStride = dw * 4;
            auto* rgbBuf = new uint8_t[dh * rgbStride];
            uint8_t* rgbPlanes[1] = { rgbBuf };
            int rgbStrides[1] = { rgbStride };

            sws_scale(sws, frame->data, frame->linesize, 0, srcH,
                      rgbPlanes, rgbStrides);

            // 直接返回画面本体(无黑底画布)——选中框才能紧贴视频画面边缘
            result = QImage(rgbBuf, dw, dh, rgbStride, QImage::Format_RGB32).copy();

            delete[] rgbBuf;
            sws_freeContext(sws);
        }
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
    avformat_close_input(&fmtCtx);

    return result.isNull() ? videoThumbFallback(filePath, size, pct) : result;
#else
    return videoThumbFallback(filePath, size, pct);
#endif
}

// ═══════════════════════════════════════════
// 视频缩略图 — QProcess 回退方案
// ═══════════════════════════════════════════
QImage Thumbnailer::videoThumbFallback(const QString& filePath, int size, int pct) {
    const QString exe = ffmpegExe();
    if (exe.isEmpty()) return {};   // 找不到已在 ffmpegExe() 里留痕,这里绝不假装成功

    const QString scalePart =
        QString("scale=%1:%2:force_original_aspect_ratio=decrease")
            .arg(size).arg(size);   // 无 pad:保留画面本体,不填黑边
    // #121 HDR(PQ/HLG)源要先把高动态压回 SDR,否则色彩是错的(饱和/明度都走偏)。
    //   滤镜串是实测跑通的写法:tonemap=hable/mobius 可用,bt2390 在本仓库这份
    //   n7.1.5 构建里直接报错,hybrid 是 8.x 才有的值。zscale/tonemap 由 #113
    //   随 gaze 分发的 ffmpeg 提供。
    const QString hdrFilter =
        QStringLiteral("zscale=transfer=linear:npl=100,format=gbrpf32le,"
                       "zscale=primaries=bt709,tonemap=tonemap=hable:desat=0,"
                       "zscale=transfer=bt709:matrix=bt709:primaries=bt709,"
                       "format=yuv420p,") + scalePart;

    // finished=false 表示进程没正常跑完(超时被杀);这种情况不该再试第二个位置,
    // 否则坏文件的代价从一次超时变成两次。log 非空时回收 ffmpeg 的输出文本。
    auto grab = [&](int seekMs, bool& finished, const QString& filter,
                    QString* log) -> QImage {
        finished = false;
        QTemporaryFile tmp(QDir::tempPath() + "/xnn_thumb_XXXXXX.png");
        tmp.setAutoRemove(false);
        if (!tmp.open()) return {};
        const QString tmpName = tmp.fileName();
        tmp.close();

        QProcess proc;
        hideConsoleWindow(proc);   // 同上:视频缩略图抽帧的 ffmpeg 也得静默起
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start(exe, {
            "-ss", QString::number(seekMs / 1000.0, 'f', 3), "-i", filePath,
            "-vframes", "1",
            "-vf", filter,
            "-q:v", "5", "-y", tmpName
        });

        if (!proc.waitForFinished(5000)) {
            proc.kill();
            proc.waitForFinished(500);
            QFile::remove(tmpName);
            return {};
        }
        finished = true;
        if (log) *log = QString::fromUtf8(proc.readAll());
        QImage img(tmpName);
        QFile::remove(tmpName);
        return img;
    };

    bool finished = false;
    const int seekMs = seekMsForPct(filePath, pct);
    QString out;
    QImage img = grab(seekMs, finished, scalePart, &out);
    // HDR 的判据不必再开一次进程去 probe:第一次 ffmpeg 的流信息行里就写着传输函数
    //   ("yuv420p10le(bt2020nc/smpte2084/bt2020)"、HLG 是 arib-std-b67)。
    //   所以 SDR 源仍是**一次进程、零额外开销**,只有 HDR 源多跑一遍带色调映射的链。
    const bool hdr = out.contains(QStringLiteral("smpte2084"))
                  || out.contains(QStringLiteral("arib-std-b67"));
    if (hdr && !img.isNull()) {
        Logger::event(QStringLiteral("videoThumb: HDR 源 → tonemap 重取 '%1'")
                          .arg(QFileInfo(filePath).fileName()));
        img = grab(seekMs, finished, hdrFilter, nullptr);
    }
    // 比 seekMs 还短的视频:在末尾之后取帧,ffmpeg 正常退出但一个字节都不写
    // (0.52 秒样片实测 -ss 1 → 0 字节,-ss 0 → 21,730 字节)→ 回零位置重试一次
    if (img.isNull() && finished && seekMs > 0)
        img = grab(0, finished, hdr ? hdrFilter : scalePart, nullptr);
    return img;
}
