// ═══════════════════════════════════════════════════════════
// PreviewPanel 的 RAW 形态(#140):优先内嵌 JPEG 预览,失败/想全解时
// 占位说明 + 「加载原始RAW」按钮(右上角) + 按需全解
//
// 2026-09-02 用户令:RAW 文件头本身带一张相机生成的完整 JPEG 预览,
// **默认就该显示它**(和相机回放、缩略图一致),不该一片灰等按钮。
// 交互模型:进入 RAW → 立即后台提取内嵌 JPEG(几乎零成本)→ 成功直接进
// 图片形态;内嵌图缺失/损坏时才落到「加载原始 RAW」全解按钮。
// 解码途中切换文件 = 不再需要预览 → m_imgReqGen 代次作废,UI 瞬间响应
// 零卡顿。线程池任务不可中途杀,但结果只对得上代号/路径/形态才应用,
// 其余一律丢弃 —— 在途任务自然跑完即弃,不为它弹任何状态。
// ═══════════════════════════════════════════════════════════

#include "previewpanel.h"
#include "rawdecode.h"
#include "constants.h"
#include "fileentry.h"

#include <QFileInfo>
#include <QTextEdit>
#include <QThreadPool>
#include <QPointer>

namespace {
const QString kRawBtnText = QString::fromUtf8("加载原始 RAW");
}

void PreviewPanel::showRawPlaceholder(const QString& path) {
    // 先进占位布局(保证任何形态都有退路),再异步提内嵌 JPEG;
    // 成功了由 onEmbeddedRawReady 直接切图片形态。
    m_mode = "raw";
    if (m_player) m_player->stop();
    if (m_liveBadge) m_liveBadge->hide();
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    setAudioChrome(false);   // 顺带收起 raw 占位(见 setAudioChrome 注);随后再亮
    m_textEdit->hide();
    m_controlBar->hide();
    m_imgSpace->hide();

    m_rawBusy = false;
    m_rawBtn->setEnabled(true);
    m_rawBtn->setText(kRawBtnText);
    QFileInfo fi(path);
    m_rawCaption->setText(QString::fromUtf8(
        "%1 · 相机 RAW 原始文件（%2）\n正在提取相机内嵌预览…")
        .arg(fi.fileName(), formatSize(fi.size())));
    m_rawBox->show();

#ifdef HAS_RAWDEC
    const quint64 gen = m_imgReqGen;   // loadFile 已为本次装载递增
    const QString p = path;
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, p, gen]() {
        const QImage img = RawDecode::decodeEmbeddedJpeg(p, 0);  // 全尺寸内嵌预览
        QMetaObject::invokeMethod(self, [self, img, p, gen]() {
            if (self) self->onEmbeddedRawReady(img, p, gen);
        }, Qt::QueuedConnection);
    });
#else
    m_rawCaption->setText(QString::fromUtf8(
        "本构建未编入 RAW 解码器（thirdparty/LibRaw 缺失）"));
#endif
}

// 内嵌 JPEG 提取完成:成功 → 图片形态;失败 → 说明 + 加载原始RAW 按钮
void PreviewPanel::onEmbeddedRawReady(const QImage& img, const QString& path, quint64 gen) {
    if (gen != m_imgReqGen || path != m_filePath || m_mode != "raw") return;
    if (!img.isNull()) {
        m_mode = "image";   // 内嵌预览即图片:缩放/拖动/查看器/全屏全套
        m_rawBox->hide();
        applyImage(img);
        return;
    }
    QFileInfo fi(path);
    m_rawCaption->setText(QString::fromUtf8(
        "%1 · 相机 RAW 原始文件（%2）\n全解为可视图像需数秒；解码途中切换文件会立即放弃。")
        .arg(fi.fileName(), formatSize(fi.size())));
}

void PreviewPanel::decodeRawAsync() {
    if (m_rawBusy) return;
#ifdef HAS_RAWDEC
    m_rawBusy = true;
    m_rawBtn->setEnabled(false);
    m_rawBtn->setText(QString::fromUtf8("正在解码…"));
    m_rawCaption->setText(QString::fromUtf8(
        "正在后台解码 RAW（大文件需数秒）——现在切换文件会立即放弃本次解码"));
    const quint64 gen = m_imgReqGen;   // loadFile 已为本次装载递增
    const QString path = m_filePath;
    QPointer<PreviewPanel> self(this);
    QThreadPool::globalInstance()->start([self, path, gen]() {
        const QImage img = RawDecode::decodeFull(path);
        // 面板已析构 → 投递自动丢弃,QImage 随 lambda 释放
        QMetaObject::invokeMethod(self, [self, img, path, gen]() {
            if (self) self->onRawDecoded(img, path, gen);
        }, Qt::QueuedConnection);
    });
#else
    m_rawCaption->setText(QString::fromUtf8(
        "本构建未编入 RAW 解码器（thirdparty/LibRaw 缺失）"));
#endif
}

void PreviewPanel::onRawDecoded(const QImage& img, const QString& path, quint64 gen) {
    m_rawBusy = false;
    // 代次/路径/形态任一对不上 = 用户已切走:直接丢弃,不碰新文件的占位状态
    if (gen != m_imgReqGen || path != m_filePath || m_mode != "raw") return;
    m_rawBtn->setEnabled(true);
    m_rawBtn->setText(kRawBtnText);
    if (img.isNull()) {
        m_rawCaption->setText(QString::fromUtf8(
            "解码失败：内置解码器不支持该 RAW 或文件已损坏"));
        return;
    }
    m_mode = "image";   // 成功 → 完整图片形态(缩放/拖动/查看器/全屏全套)
    m_rawBox->hide();
    applyImage(img);
}
