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

// ── #82:Markdown 以渲染后的 HTML 展示 ──
void PreviewPanel::showMarkdown(const QString& path) {
    m_mode = "text";
    if (m_player) m_player->stop();
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_controlBar->hide();
    if (m_imgSpace) m_imgSpace->hide();
    if (m_pdfBar) m_pdfBar->hide();
    if (m_liveBadge) m_liveBadge->hide();
    // #240:自动换行开关(设置→浏览器→预览;右键文本预览同样可切,即时生效)
    m_textEdit->setWordWrapMode(pp_impl::s_bool("Preview/textWrap", true)
        ? QTextOption::WordWrap : QTextOption::NoWrap);

    const QString html = Md::renderFile(path);
    m_textEdit->setHtml(html.isEmpty()
        ? gazeTr("<p style='color:#C0C0C6'>无法读取文件</p>") : html);
    m_textEdit->show();
}

// ── #82:PDF 预览(Ghostscript 渲染当前页 + 页导航条)──
void PreviewPanel::showPdf(const QString& path) {
    m_mode = "pdf";
    if (m_player) m_player->stop();
    m_placeholder->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_textEdit->hide();
    m_controlBar->hide();
    if (m_imgSpace) m_imgSpace->hide();
    if (m_liveBadge) m_liveBadge->hide();

    m_pdfPath = path;
    // 换文件才重置页码;同一文件翻页时沿用
    if (m_pdfPath != m_pdfLastPath) { m_pdfPage = 1; m_pdfLastPath = path; m_pdfPages = 0; }
    updatePdfBar();
    requestPdf(true);   // 第一次要把页数一起问出来
}

// 渲染在后台线程跑(Ghostscript 是外部进程,首帧可能上百毫秒,不能卡 UI)
void PreviewPanel::renderPdfPage() { requestPdf(false); }

// pageCount 同样是起子进程 —— 以前它写在 GUI 线程里(最长阻塞 6 秒),
// 与 #70「GUI 线程同步等待外部进程」是同一类坑,一并挪进线程池。
void PreviewPanel::requestPdf(bool needPageCount) {
    if (m_pdfBusy) return;
    m_pdfBusy = true;
    const QString path = m_pdfPath;
    const int page = m_pdfPage;
    QPointer<PreviewPanel> self(this);   // worker 回调访问 this 前必须确认还活着
    QThreadPool::globalInstance()->start([self, path, page, needPageCount]() {
        const int n = needPageCount ? Pdf::pageCount(path) : 0;
        QImage img = Pdf::renderPage(path, page);
        // 内置 gs/ 缺失或该页渲染失败 → 退回 Windows Shell 缩略图(Explorer 首页缩略图)
        if (img.isNull()) img = Thumbnailer::shellThumbFor(path, 1024);
        QMetaObject::invokeMethod(self, [self, img, path, page, n]() {
            if (!self) return;
            self->m_pdfBusy = false;
            if (path != self->m_pdfPath) { self->renderPdfPage(); return; }   // 期间换了文件
            if (n > 0) self->m_pdfPages = n;
            if (img.isNull()) {
                delete self->m_origPix;
                self->m_origPix = nullptr;
                self->m_placeholder->setText(
                    gazeTr("无法预览该 PDF\n(内置 gs/ 缺失或文件无法渲染)"));
                self->m_placeholder->show();
                self->m_imgLabel->hide();
                self->updatePdfBar();
                return;
            }
            delete self->m_origPix;
            self->m_origPix = new QPixmap(QPixmap::fromImage(img));
            self->m_imgLabel->show();
            self->m_placeholder->hide();
            self->fitAuto();
            self->updatePdfBar();
            if (page != self->m_pdfPage) self->renderPdfPage();               // 期间翻了页
        }, Qt::QueuedConnection);
    });
}

void PreviewPanel::pdfGotoPage(int page) {
    if (m_mode != "pdf" || page < 1) return;
    if (m_pdfPages > 0 && page > m_pdfPages) page = m_pdfPages;
    if (page == m_pdfPage) return;
    m_pdfPage = page;
    updatePdfBar();
    renderPdfPage();
}

void PreviewPanel::updatePdfBar() {
    if (m_mode != "pdf" || !m_pdfBar) { if (m_pdfBar) m_pdfBar->hide(); return; }
    const int total = m_pdfPages;
    m_pdfPrev->setEnabled(m_pdfPage > 1);
    m_pdfNext->setEnabled(total <= 0 || m_pdfPage < total);
    m_pdfLabel->setText(total > 0
        ? gazeTr("%1 / %2 页").arg(m_pdfPage).arg(total)
        : gazeTr("第 %1 页").arg(m_pdfPage));
    m_pdfLabel->adjustSize();
    m_pdfBar->adjustSize();
    m_pdfBar->move((width() - m_pdfBar->width()) / 2, height() - m_pdfBar->height() - 10);
    m_pdfBar->raise();
    m_pdfBar->show();
}

void PreviewPanel::showText(const QString& path) {
    m_mode = "text";
    if (m_player) m_player->stop();
    m_placeholder->hide();
    m_imgLabel->hide();
    m_videoWidget->hide();
    setAudioChrome(false);
    m_controlBar->hide();
    m_imgSpace->hide();
    if (m_liveBadge) m_liveBadge->hide();
    // #240:自动换行开关(设置→浏览器→预览;右键文本预览同样可切,即时生效)
    m_textEdit->setWordWrapMode(pp_impl::s_bool("Preview/textWrap", true)
        ? QTextOption::WordWrap : QTextOption::NoWrap);

    if (m_pdfBar) m_pdfBar->hide();   // pdf 页导航条只属于 pdf 形态,别漏进来
    QFile f(path);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        // #111:行数与"单行字符数"都要截。只卡字节数不够 —— 实测一个 512KB 的
        // 无空格单行(压缩 JS/base64)setPlainText 要 62 秒,界面彻底冻死。
        // 上限与实测依据都写在 textlimit.h 头部。
        bool byteCut = false;
        qint64 total = 0;
        const QString raw = TextCut::readHead(f, &byteCut, &total);
        f.close();
        // 一个字节都没读出来但文件不是空的:读取被占用/权限挡了 —— 如实报错,
        // 不能走"已截断"的口径(此前小文件被占用时会谎报"文件 0 KB 仅读取前 0 KB")
        if (raw.isEmpty() && total > 0) {
            m_textEdit->setPlainText(gazeTr(
                "无法读取文件内容(可能正被其他程序占用,或没有读取权限)"));
            m_textEdit->show();
            return;
        }
        m_textEdit->setPlainText(TextCut::apply(raw, byteCut, total));
        m_textEdit->show();
    } else {
        m_textEdit->setPlainText(gazeTr("无法读取文件"));
        m_textEdit->show();
    }
}
