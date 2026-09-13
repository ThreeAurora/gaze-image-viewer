#include "previewpanel.h"
#include "livephoto.h"
#include "thumbnailer.h"
#include "logger.h"
#include "wicdecode.h"
#include "settings.h"
#include "labelstore.h"
#include "markdown.h"
#include "pdfrender.h"
#include "textlimit.h"
#include "imgproc.h"
#include "constants.h"
#include "viewerhotkeys.h"
#include "shelldelete.h"
#include "fileentry.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QKeyEvent>
#include <QFileInfo>
#include <QImageReader>
#include <QPainter>
#include <QtMath>
#include <QApplication>
#include <QScreen>
#include <QDir>
#include <QSplitter>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QDesktopServices>
#include <QMimeData>
#include <QMediaDevices>
#include <QWidgetAction>
#include <QThreadPool>
#include <QTextEdit>
#include <QFile>
#include <QMetaObject>
#include <QClipboard>
#include <QBrush>
#include <QScrollBar>
#include <cmath>
#include <QStyle>
#include <QWidgetAction>
#include <QMenu>
#include <QVideoFrame>
#include <QVideoSink>
#include "previewpanel_internal.h"

// mm:ss / h:mm:ss。GIF 时间轴与视频用同一套时间写法,切换类型时读数不会两种长相
static QString formatMs(qint64 ms)
{
    int sec = static_cast<int>(ms / 1000);
    if (sec < 0) sec = 0;
    const int h = sec / 3600;
    return h > 0
        ? QString("%1:%2:%3").arg(h).arg((sec % 3600) / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'))
        : QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
}

// 走一遍 GIF 块链拿逐帧时长(ms)。只解两类块:
//   0x21 0xF9 Graphic Control Extension → 它后面那一帧的 delay
//   0x2C      Image Descriptor          → 一帧到此结束
// 其余扩展块整体按"子块链(0x00 收尾)"跳过,所以注释/应用扩展里长得像
// GCE 的字节不会被误读成时长。
// 时长 0 = 文件没写 → 兜 100ms。这条是 GIF 社区的通行惯例,不是本机实测的
// QMovie 行为(QMovie 对极短时长的钳位规则没有公开口径,只能按惯例取一致值)。
static bool parseGifDelays(const QString& path, QVector<int>& outMs)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    // 预览是热路径:超大 GIF 不在 GUI 线程整读,交给 buildGifTimeline 的退路分支
    if (f.size() > 64LL * 1024 * 1024) { f.close(); return false; }
    const QByteArray d = f.readAll();
    f.close();
    const int n = d.size();
    if (n < 14 || memcmp(d.constData(), "GIF8", 4) != 0) return false;

    auto u8 = [&](int at) -> int { return static_cast<quint8>(d.at(at)); };
    int pos = 13;                                   // 6 签名 + 7 逻辑屏幕描述符
    const int gct = u8(10);                         // LSD packed:0x80=有全局色表
    if (gct & 0x80) pos += 3 * (2 << (gct & 0x07));

    int curDelay = -1;
    while (pos < n) {
        const int b = u8(pos);
        if (b == 0x3B) break;                       // trailer
        if (b == 0x21) {                            // extension
            if (pos + 1 >= n) return false;
            const int label = u8(pos + 1);
            pos += 2;
            if (label == 0xF9) {
                if (pos + 4 >= n || u8(pos) != 4) return false;
                curDelay = (u8(pos + 3) << 8 | u8(pos + 2)) * 10;   // 百分之一秒
                pos += 5;                           // 长度字节 + 4 字节数据
            }
            while (pos < n) {                       // 跳过这个扩展剩下的子块
                const int len = u8(pos); pos += 1;
                if (len == 0) break;
                pos += len;
            }
            continue;
        }
        if (b == 0x2C) {                            // image descriptor = 一帧
            if (pos + 9 >= n) return false;
            const int packed = u8(pos + 9);
            pos += 10;
            if (packed & 0x80) pos += 3 * (2 << (packed & 0x07));   // 局部色表
            if (pos >= n) return false;
            pos += 1;                               // LZW 最小码长
            while (pos < n) {                       // 图像位数据子块链
                const int len = u8(pos); pos += 1;
                if (len == 0) break;
                pos += len;
            }
            // delay 写 0 的 GIF(大量工具会这么生成)按社区惯例兜 100ms:
            // 没写(<0)与明确写 0 都不该按 0 兑成 20ms/帧的狂奔
            outMs << (curDelay <= 0 ? 100 : curDelay);
            curDelay = -1;
            continue;
        }
        return false;                               // 未知块:再往下读没有意义
    }
    return !outMs.isEmpty();
}

// GIF:第一帧当"整图"走静态图那套 fit/render 定几何,动画期间只换像素不改尺寸。
// 旧实现只 setMovie 从不定尺寸:蓝框套的是**上一张图**的大小(#103);
// 而 label 从没定过尺寸时(目录里第一张就是 GIF)只剩左上角一块,
// 看起来就是"GIF 放不了"(#96)。
void PreviewPanel::showGif(const QString& path) {
    auto* reader = new QImageReader(path);
    reader->setDecideFormatFromContent(true);
    QImage first = reader->read();
    if (first.isNull()) {
        delete reader;
        showImageHint(gazeTr("无法加载动画"));
        return;
    }

    stopMovie();                 // 上一张 GIF 先回收(不依赖调用方顺序)
    m_gifReader = reader;        // 自管播放时钟(见 gifPlayTick);跳帧 API 恒失败,弃用
    m_isGif = true;              // stopMovie 之后才立,否则会被自己清掉
    m_gifFrameIdx = 0;
    m_gifNext = 1;               // 上面已 read() 掉帧 0 → 游标实打实停在帧 1
    delete m_origPix;
    m_origPix = new QPixmap(QPixmap::fromImage(first));
    m_scale       = 1.0;
    m_tempZoom    = false;
    m_ctrlZoomed  = false;
    m_procKey.clear();           // gamma/sharpen 缓存与导航小窗缩略图一并作废
    m_panKey.clear();
    gifCachePut(0, first);       // 帧 0 入缓存:回卷和"拖回开头"都不用重解
    blitGifFrame(first);         // 第一帧直贴;几何未定时 blit 只存帧,render() 会补贴

    m_imgLabel->setText(QString());   // 清掉可能残留的"无法加载"提示
    m_imgLabel->show();
    applyGifChrome();                 // #97:GIF 亮出与视频同一条控制栏(先亮,fit 才扣得掉它)
    fitAuto();                        // render() 由此定 label 几何(第一帧 × 缩放)
    m_navigating = false;             // 与 applyImage 同口径:缩放已定型
    applyViewerChrome();
    buildGifTimeline();               // 帧 0 已显示,顺序游标就位
    // Viewer/disableAnimation:停在第一帧(不是"没有动画这回事",按播放键仍可播)
    m_gifPaused = pp_impl::s_bool("Viewer/disableAnimation", false);
    setGifPaused(m_gifPaused);        // 播放键图标唯一刷新出口;恢复播放即排第一拍
}

// GIF 取帧的唯一出口:存原始帧(m_gifPix),按 label 当前尺寸缩放后贴上。
// 尺寸已经相等(1:1 或恰好适应)时跳过缩放,省下每帧一次重采样
void PreviewPanel::blitGifFrame(const QImage& img) {
    if (img.isNull()) return;
    m_gifPix = QPixmap::fromImage(img);
    const QSize want = m_imgLabel->size();
    if (want.isEmpty()) return;    // 几何未定:fitAuto/render() 会带着 m_gifPix 再贴
    m_imgLabel->setPixmap(m_gifPix.size() == want
                              ? m_gifPix
                              : m_gifPix.scaled(want, Qt::IgnoreAspectRatio,
                                                Qt::SmoothTransformation));
}

// ── GIF 时间轴(#97):进度条的值 = 播放头(ms),与视频同一口径 ──

void PreviewPanel::buildGifTimeline()
{
    m_gifDelay.clear();
    m_gifStart.clear();
    if (!m_isGif) return;

    QVector<int> delays;
    const bool parsed = parseGifDelays(m_filePath, delays);
    if (parsed) {
        m_gifDelay = delays;
    } else if (m_gifReader && m_gifReader->imageCount() > 0) {
        // 退路:字节解析没成功(非标准块链/超大文件)但解码器报得出帧数
        // → 用统一时长撑出一条能拖能定位的轴,刻度不精确但比没有轴好
        m_gifDelay = QVector<int>(m_gifReader->imageCount(), 100);
    } else {
        Logger::event("gif timeline: no per-frame durations available");
    }
    if (m_gifDelay.isEmpty()) {
        // 连帧数都报不出来:轴和时长一律归零,绝不能留着上一条视频的数字装样子
        m_progress->setRange(0, 0);
        m_timeLabel->setText(QStringLiteral("0:00 / 0:00"));
        return;
    }
    m_gifStart.reserve(m_gifDelay.size() + 1);
    int acc = 0;
    for (int i = 0; i < m_gifDelay.size(); ++i) { m_gifStart << acc; acc += m_gifDelay[i]; }
    m_gifStart << acc;
    m_progress->setRange(0, qMax(1, acc));
    Logger::event(QStringLiteral("gif timeline: frames=%1 total=%2ms src=%3")
                      .arg(m_gifDelay.size()).arg(acc)
                      .arg(parsed ? QStringLiteral("parsed") : QStringLiteral("uniform")));
    gifSyncToFrame(m_gifFrameIdx);
}

void PreviewPanel::gifSyncToFrame(int f)
{
    if (!m_isGif || m_gifStart.isEmpty()) return;
    if (f < 0 || f >= m_gifDelay.size()) return;
    const int total = m_gifStart.last();
    const int pos = m_gifStart[f];
    // 擦洗中别回写句柄:否则播放帧的 frameChanged 会把用户正拖着的滑块拽走
    if (!m_progress->isSliderDown())
        m_progress->setValue(qBound(0, pos, m_progress->maximum()));
    const int shown = m_timeRemaining ? qMax(0, total - pos) : pos;
    m_timeLabel->setText(formatMs(shown) + " / " + formatMs(total));
}

void PreviewPanel::gifSeekMs(int ms)
{
    if (!m_isGif || !m_gifReader || m_gifDelay.isEmpty()) return;
    const int total = m_gifStart.last();
    const int want = qBound(0, ms, qMax(0, total - 1));
    int f = m_gifDelay.size() - 1;
    for (int i = 0; i < m_gifDelay.size(); ++i) {
        if (want < m_gifStart[i] + m_gifDelay[i]) { f = i; break; }
    }
    gifRequestSeek(f);
}

// 擦洗合帧:一次拖动在一个事件循环批次里能塞进十几个 move,而每一帧都得同步
// 解出来(实测 720p GIF 从文件头解到第 37 帧 = 225ms)——逐 move 解码就是
// "往回拖直接卡死"。这里只记下最新目标帧,用 0ms 单发定时器把同批次里的请求
// 并成最后一次,落拍时才真正解码。
void PreviewPanel::gifRequestSeek(int f)
{
    if (f < 0 || f >= m_gifDelay.size()) return;
    m_gifWant = f;
    m_gifSeekTimer->start();
}

void PreviewPanel::gifApplySeek()
{
    const int f = m_gifWant;
    m_gifWant = -1;
    if (!m_isGif || f < 0) return;
    if (f == m_gifFrameIdx) { gifSyncToFrame(f); return; }   // 已经是这一帧,不必再解
    QImage img;
    if (!gifDecodeTo(f, img)) {
        // 解不出来绝不能静默返回:那正是"点了没反应"的长相。
        // 留下日志并把播放头拉回真正显示的那一帧,滑块不会停在画面之外的位置。
        Logger::event(QStringLiteral("gif seek: frame %1 decode failed").arg(f));
        gifSyncToFrame(m_gifFrameIdx);
        return;
    }
    m_gifFrameIdx = f;
    blitGifFrame(img);
    gifSyncToFrame(f);
    if (m_gifPlaying) gifScheduleNext();   // 播放中:按新帧延迟重排下一拍
}

// ── GIF 自管播放时钟 + 跳帧(2026-08-31 重写,#94)──────────────────────────
// 两条实测事实决定了这里的形态(探针 cache/tmp/gif_seek_probe、gif_perf_probe):
//   1) Qt 6.8.3 的 jumpToImage / jumpToNextImage 对 GIF 一律返回 false
//      → 旧代码 gifSeekMs 里的 jumpToImage 恒失败直接 return,点击/拖动静默失效;
//        gifAdvance 回卷处的同一句恒失败 → 播完自动暂停("往前它就暂停")。
//   2) 逐帧解码/blit 都很便宜(320px 每帧 1.4ms、720p 6.4ms → 156fps 余量)
//      → 卡顿不在缩放上,在"没有可用的跳帧"。
// 所以取帧只留一条可靠路径:顺序解到位,再用帧缓存把重复擦洗摊平成命中。
bool PreviewPanel::gifDecodeTo(int f, QImage& out)
{
    const int count = m_gifDelay.size();
    if (count <= 0 || f < 0 || f >= count) return false;

    const auto cached = m_gifCache.constFind(f);
    if (cached != m_gifCache.constEnd()) {
        out = cached.value();
        m_gifNext = -1;      // 游标与 f 再无关系:下一拍要么命中缓存,要么重建重解
        return true;
    }

    int from = m_gifNext;
    if (from < 0 || from > f) {                 // 回头跳/游标失效 → 重建从 0 解
        delete m_gifReader;
        m_gifReader = new QImageReader(m_filePath);
        m_gifReader->setDecideFormatFromContent(true);
        from = 0;
    }
    QImage img;
    for (int i = from; i <= f; ++i) {
        img = m_gifReader->read();
        if (img.isNull()) { m_gifNext = -1; return false; }
        gifCachePut(i, img);
        m_gifNext = i + 1;                      // 每读成一帧,游标就实打实前移一帧
    }
    out = img;
    return true;
}

// 预算 48MB:720p 一帧 3.7MB ≈ 13 帧,足够把来回擦洗的常见区间兜住。
// 超预算淘汰"离新播放头最远"的帧——保留的是围绕当前位置的窗口,不是最早解的那批。
void PreviewPanel::gifCachePut(int f, const QImage& img)
{
    static const qint64 kBudget = 48LL * 1024 * 1024;
    if (m_gifCache.contains(f)) return;
    m_gifCache.insert(f, img);
    m_gifCacheBytes += qint64(img.sizeInBytes());
    while (m_gifCacheBytes > kBudget && m_gifCache.size() > 1) {
        int dropKey = -1;
        qint64 best = -1;
        for (auto it = m_gifCache.constBegin(); it != m_gifCache.constEnd(); ++it) {
            const qint64 d = qAbs(qint64(it.key()) - qint64(f));
            if (d > best) { best = d; dropKey = it.key(); }
        }
        if (dropKey < 0) break;
        m_gifCacheBytes -= qint64(m_gifCache.take(dropKey).sizeInBytes());
    }
}

void PreviewPanel::gifScheduleNext() {
    if (!m_gifPlaying || !m_isGif || !m_gifReader) return;
    if (m_gifDelay.size() < 2) return;     // 单帧 GIF:静态画面,无需时钟
    const int d = (m_gifFrameIdx >= 0 && m_gifFrameIdx < m_gifDelay.size())
                      ? m_gifDelay[m_gifFrameIdx] : 100;
    m_gifPlayTimer->start(qBound(20, d, 60000));   // 成员单发定时器:重排即取消上一拍
}

void PreviewPanel::gifPlayTick() {
    if (!m_gifPlaying || !m_isGif || !m_gifReader) return;   // 暂停/切文件后未决拍自灭
    // 擦洗期间画面归滑块管:这一拍只重排不解帧,否则"拖到 30 帧"会被
    // 播放拍接着推到 31/32,松手前画面还在自己走。
    if (m_progress->isSliderDown()) { gifScheduleNext(); return; }
    if (!gifAdvance()) { m_gifPlaying = false; return; }     // 解码失败:停,别自旋
    gifScheduleNext();
}

bool PreviewPanel::gifAdvance() {
    const int count = m_gifDelay.size();
    if (count <= 0) return false;
    const int f = (m_gifFrameIdx + 1 >= count) ? 0 : m_gifFrameIdx + 1;   // 到尾回卷
    QImage img;
    if (!gifDecodeTo(f, img)) return false;
    m_gifFrameIdx = f;
    blitGifFrame(img);
    gifSyncToFrame(f);
    return true;
}

void PreviewPanel::setGifPaused(bool p)
{
    if (!m_isGif || !m_gifReader) return;
    m_gifPaused = p;
    m_gifPlaying = !p;
    if (p) m_gifPlayTimer->stop();         // 暂停即在源头掐掉未决拍,不靠 tick 里自灭
    else   gifScheduleNext();              // 恢复:立即排下一拍
    m_btnPlay->setIcon(pp_impl::whiteIcon(style()->standardIcon(
        p ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause)));
}

void PreviewPanel::applyGifChrome()
{
    m_controlBar->show();
    m_imgSpace->show();                 // 图片形态没有别的 stretch 项,靠吸收器把栏压到底部
    m_imgLabel->raise();                // 画面必须压过吸收器(z 序按创建序,吸收器在后)
    m_controlBar->raise();              // 控制栏保持最顶(叠在画面底边之上)
    m_btnVolume->hide();                // GIF 没有音轨,留着音量键是假的
}

void PreviewPanel::stopMovie() {
    // GIF 状态先清再判空:非 GIF 路径也会调这里(换文件/清空),
    // 留着 m_isGif 会让控制栏和单击判定继续按 GIF 走
    m_isGif = false;
    m_gifPaused = false;
    m_gifPlaying = false;                  // 未决拍跟着 stop() 一起灭,不留到下一个文件
    m_gifToggleArm = false;
    m_gifPressPos = QPoint();
    m_gifFrameIdx = 0;
    m_gifPix = QPixmap();
    m_gifDelay.clear();
    m_gifStart.clear();
    m_gifNext = -1;
    m_gifWant = -1;
    if (m_gifPlayTimer) m_gifPlayTimer->stop();
    if (m_gifSeekTimer) m_gifSeekTimer->stop();   // 连接建在构造函数,这里只停不拆
    m_gifCache.clear();                    // 帧缓存最高 48MB,换文件必须当场交还
    m_gifCacheBytes = 0;
    delete m_gifReader;                    // QImageReader 非 QObject,手动回收
    m_gifReader = nullptr;
}
