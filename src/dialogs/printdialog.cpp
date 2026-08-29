#include "printdialog.h"

#include "constants.h"
#include "imgproc.h"
#include "printlayout.h"
#include "settings.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFileInfo>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPageLayout>
#include <QPainter>
#include <QPointer>
#include <QPrintEngine>
#include <QPrinter>
#include <QPrinterInfo>
#include <QProgressDialog>
#include <QPushButton>
#include <QRadioButton>
#include <QRegularExpression>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSet>
#include <QSpinBox>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <memory>

// ═══════════════════════════════════════════
// 这里只做三件事:界面 ↔ PrintOptions、QPrinter 几何 ↔ 预览画布、取图器。
// 排版数学一行都不写 —— 全在 printlayout.cpp,预览与出图共用。
// ═══════════════════════════════════════════

namespace {

constexpr int   kPrintDpi     = 300;    // 出图分辨率。图像打印的天花板,且"原始尺寸"档的物理
                                        // 尺寸与它无关(scale=dpi/文件dpi,像素/mm 比值不变)
constexpr int   kPrevMaxSide  = 1024;   // 预览解码上限;"原始尺寸"档例外(见 previewMaxSide)
constexpr qreal kPrevDpiMax   = 200;    // 预览画布 dpi 上限(A4 长边 ≈ 2339px,再高纯浪费)
constexpr int   kPadDip       = 12;     // 预览里纸张四周的"桌面"留白

QStringList imageOnly(const QStringList& in) {
    QStringList out;
    for (const QString& p : in)
        if (IMAGE_EXTS.count("." + QFileInfo(p).suffix().toLower())) out << p;
    return out;
}

// 下拉框一律按 data 取值:列表文案改了不会静默错位(枚举值不靠 index 巧合)
void addCb(QComboBox* cb, const QString& text, int val) {
    cb->addItem(text, val);
}
int  cbVal(const QComboBox* cb, int def) {
    const QVariant v = cb->currentData();
    return v.isValid() ? v.toInt() : def;
}
void cbSetVal(QComboBox* cb, int val) {
    const int i = cb->findData(val);
    if (i >= 0) cb->setCurrentIndex(i);
}

QString paperLabel(const QPageSize& s) {
    const QSizeF mm = s.size(QPageSize::Millimeter);
    QString name = s.name();
    if (name.isEmpty()) name = QString::fromUtf8("自定义");
    return QStringLiteral("%1  %2×%3mm").arg(name)
        .arg(QString::number(mm.width(), 'f', 0), QString::number(mm.height(), 'f', 0));
}

// 驱动一个纸张都不报时的兜底清单(虚拟打印机/残缺驱动实测可能给空表)
QVector<QPageSize> fallbackPapers() {
    static const QPageSize::PageSizeId ids[] = {
        QPageSize::A3, QPageSize::A4, QPageSize::A5, QPageSize::A6,
        QPageSize::B4, QPageSize::B5, QPageSize::Letter, QPageSize::Legal,
        QPageSize::Postcard
    };
    QVector<QPageSize> v;
    for (auto id : ids) v.append(QPageSize(id));
    return v;
}

} // namespace

// ─────────────────────────────────────────
// 入口
// ─────────────────────────────────────────
void PrintDialog::printImages(QWidget* parent, const QStringList& candidates) {
    const QStringList imgs = imageOnly(candidates);
    if (imgs.isEmpty()) {
        QMessageBox::information(parent, QString::fromUtf8("打印"),
            QString::fromUtf8("所选内容里没有可打印的图片(共 %1 项)。")
                .arg(candidates.size()));
        return;
    }
    PrintDialog dlg(parent, imgs);
    dlg.exec();
}

PrintDialog::PrintDialog(QWidget* parent, const QStringList& imagePaths)
    : QDialog(parent), m_paths(imagePaths)
{
    setWindowTitle(QString::fromUtf8("打印"));
    resize(1060, 680);
    m_exifRotate = AppSettings::instance().get("General/exifRotate", true).toBool();
    buildUi();
    loadPrinters();
    updateSummary();

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        if (m_printing) return;
        if (m_geometryDirty) { configurePrinter(); m_geometryDirty = false; }
        m_previewPage = qBound(0, m_previewPage, qMax(0, pageCount() - 1));
        requestPageDecode();
    });
    m_geometryDirty = true;
    m_debounce->start();   // 首次出图延到事件循环,视口尺寸此时才可信
}

PrintDialog::~PrintDialog() = default;

void PrintDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (m_printing || !m_debounce) return;
    m_geometryDirty = false;   // 换纸/换方向没变,只是重出图
    m_debounce->start();
}

// ─────────────────────────────────────────
// 界面
// ─────────────────────────────────────────
void PrintDialog::buildUi() {
    AppSettings& st = AppSettings::instance();

    // ── 左栏 ──
    auto* printerBox = new QGroupBox(QString::fromUtf8("打印机"), this);
    {
        auto* f = new QFormLayout(printerBox);
        m_printerCb = new QComboBox(printerBox);
        m_printerCb->setMinimumWidth(250);
        f->addRow(QString::fromUtf8("名称"), m_printerCb);
        m_copiesCbLabel: ;
        m_copiesLbl = new QLabel(QString::fromUtf8("份数"), printerBox);
        m_copiesSpn = new QSpinBox(printerBox);
        m_copiesSpn->setRange(1, 99);
        m_copiesSpn->setValue(st.get("Print/copies", 1).toInt());
        m_copiesSpn->setSuffix(QString::fromUtf8(" 份"));
        f->addRow(m_copiesLbl, m_copiesSpn);
    }

    auto* pageBox = new QGroupBox(QString::fromUtf8("纸张"), this);
    {
        auto* f = new QFormLayout(pageBox);
        m_paperCb = new QComboBox(pageBox);
        f->addRow(QString::fromUtf8("纸张"), m_paperCb);
        m_orientCb = new QComboBox(pageBox);
        addCb(m_orientCb, QString::fromUtf8("纵向"), 0);
        addCb(m_orientCb, QString::fromUtf8("横向"), 1);
        f->addRow(QString::fromUtf8("方向"), m_orientCb);
        m_marginSpn = new QDoubleSpinBox(pageBox);
        m_marginSpn->setRange(0, 50);
        m_marginSpn->setDecimals(1);
        m_marginSpn->setSingleStep(0.5);
        m_marginSpn->setSuffix(QString::fromUtf8(" mm"));
        m_marginSpn->setValue(st.get("Print/marginMm", 5.0).toDouble());
        f->addRow(QString::fromUtf8("页边距"), m_marginSpn);
        m_gapSpn = new QDoubleSpinBox(pageBox);
        m_gapSpn->setRange(0, 30);
        m_gapSpn->setDecimals(1);
        m_gapSpn->setSingleStep(0.5);
        m_gapSpn->setSuffix(QString::fromUtf8(" mm"));
        m_gapSpn->setValue(st.get("Print/gapMm", 2.0).toDouble());
        f->addRow(QString::fromUtf8("图间距"), m_gapSpn);
    }

    auto* layBox = new QGroupBox(QString::fromUtf8("版式"), this);
    {
        auto* f = new QFormLayout(layBox);
        m_perPageCb = new QComboBox(layBox);
        for (int n : {1, 2, 3, 4, 6, 9})
            addCb(m_perPageCb, QString::fromUtf8("%1 张/页").arg(n), n);
        cbSetVal(m_perPageCb, st.get("Print/perPage", 1).toInt());
        f->addRow(QString::fromUtf8("每页"), m_perPageCb);

        m_fitCb = new QComboBox(layBox);
        addCb(m_fitCb, QString::fromUtf8("适应边框"), PrintFit::Fit);
        addCb(m_fitCb, QString::fromUtf8("不放大"),   PrintFit::NoUpscale);
        addCb(m_fitCb, QString::fromUtf8("原始尺寸"), PrintFit::Actual);
        addCb(m_fitCb, QString::fromUtf8("填满裁边"), PrintFit::Fill);
        cbSetVal(m_fitCb, st.get("Print/fit", PrintFit::Fit).toInt());
        f->addRow(QString::fromUtf8("缩放"), m_fitCb);

        m_captionCb = new QComboBox(layBox);
        addCb(m_captionCb, QString::fromUtf8("无"),           PrintCaption::None);
        addCb(m_captionCb, QString::fromUtf8("文件名"),        PrintCaption::Name);
        addCb(m_captionCb, QString::fromUtf8("文件名+尺寸"),   PrintCaption::NameSize);
        addCb(m_captionCb, QString::fromUtf8("文件名+日期"),   PrintCaption::NameDate);
        cbSetVal(m_captionCb, st.get("Print/caption", PrintCaption::Name).toInt());
        f->addRow(QString::fromUtf8("说明文字"), m_captionCb);

        m_captionPt = new QSpinBox(layBox);
        m_captionPt->setRange(6, 24);
        m_captionPt->setValue(st.get("Print/captionPt", 9).toInt());
        m_captionPt->setSuffix(QString::fromUtf8(" pt"));
        f->addRow(QString::fromUtf8("文字字号"), m_captionPt);

        m_bgCb = new QComboBox(layBox);
        addCb(m_bgCb, QString::fromUtf8("白色"), PrintBg::White);
        addCb(m_bgCb, QString::fromUtf8("黑色"), PrintBg::Black);
        addCb(m_bgCb, QString::fromUtf8("不填充"), PrintBg::None);
        cbSetVal(m_bgCb, st.get("Print/background", PrintBg::White).toInt());
        f->addRow(QString::fromUtf8("背景"), m_bgCb);

        m_grayChk = new QCheckBox(QString::fromUtf8("转灰度"), layBox);
        m_grayChk->setChecked(st.get("Print/grayscale", false).toBool());
        m_borderChk = new QCheckBox(QString::fromUtf8("每张图描边"), layBox);
        m_borderChk->setChecked(st.get("Print/border", false).toBool());
        auto* chk = new QHBoxLayout();
        chk->addWidget(m_grayChk);
        chk->addWidget(m_borderChk);
        f->addRow(QString(), chk);
    }

    auto* rangeBox = new QGroupBox(QString::fromUtf8("打印范围"), this);
    {
        auto* g = new QGridLayout(rangeBox);
        g->setContentsMargins(9, 6, 9, 6);
        m_rAll   = new QRadioButton(QString::fromUtf8("全部"), rangeBox);
        m_rOdd   = new QRadioButton(QString::fromUtf8("奇数页"), rangeBox);
        m_rEven  = new QRadioButton(QString::fromUtf8("偶数页"), rangeBox);
        m_rRange = new QRadioButton(QString::fromUtf8("页码"), rangeBox);
        m_rangeEd = new QLineEdit(rangeBox);
        m_rangeEd->setPlaceholderText(QStringLiteral("1-3,5,8-"));
        m_rangeEd->setEnabled(false);
        const int subset = st.get("Print/subset", 0).toInt();
        (subset == 1 ? m_rOdd : subset == 2 ? m_rEven : subset == 3 ? m_rRange : m_rAll)->setChecked(true);
        m_rangeEd->setText(st.get("Print/range").toString());
        g->addWidget(m_rAll,   0, 0, 1, 2);
        g->addWidget(m_rOdd,   1, 0, 1, 2);
        g->addWidget(m_rEven,  2, 0, 1, 2);
        g->addWidget(m_rRange, 3, 0);
        g->addWidget(m_rangeEd, 3, 1);
        g->setColumnStretch(1, 1);
    }

    auto* left = new QVBoxLayout();
    left->addWidget(printerBox);
    left->addWidget(pageBox);
    left->addWidget(layBox);
    left->addWidget(rangeBox);
    left->addStretch(1);
    auto* leftWrap = new QWidget(this);
    leftWrap->setLayout(left);
    leftWrap->setFixedWidth(318);

    // ── 右栏:预览 ──
    m_prevBtn = new QToolButton(this);
    m_prevBtn->setArrowType(Qt::LeftArrow);
    m_prevBtn->setToolTip(QString::fromUtf8("上一页"));
    m_nextBtn = new QToolButton(this);
    m_nextBtn->setArrowType(Qt::RightArrow);
    m_nextBtn->setToolTip(QString::fromUtf8("下一页"));
    m_pageLbl = new QLabel(this);
    auto* nav = new QHBoxLayout();
    nav->addWidget(m_prevBtn);
    nav->addWidget(m_pageLbl, 1, Qt::AlignCenter);
    nav->addWidget(m_nextBtn);

    m_scroll  = new QScrollArea(this);
    m_scroll->setFrameShape(QFrame::NoFrame);
    m_scroll->setAlignment(Qt::AlignCenter);
    m_scroll->setWidgetResizable(true);
    m_preview = new QLabel(m_scroll);
    m_preview->setAlignment(Qt::AlignCenter);
    m_scroll->setWidget(m_preview);

    m_summary = new QLabel(this);
    m_summary->setWordWrap(true);
    m_summary->setTextFormat(Qt::RichText);

    auto* right = new QVBoxLayout();
    right->addLayout(nav);
    right->addWidget(m_scroll, 1);
    right->addWidget(m_summary);

    auto* top = new QHBoxLayout();
    top->addWidget(leftWrap);
    top->addWidget(right, 1);

    auto* btns = new QDialogButtonBox(Qt::Horizontal, this);
    m_printBtn = btns->addButton(QString::fromUtf8("打印"), QDialogButtonBox::AcceptRole);
    m_printBtn->setDefault(true);
    btns->addButton(QDialogButtonBox::Cancel);

    auto* outer = new QVBoxLayout(this);
    outer->addLayout(top, 1);
    outer->addWidget(btns);

    // ── 接线:所有版式改动 → 去抖重出图 ──
    auto touch = [this]() { scheduleRefresh(); };
    connect(m_perPageCb, qOverload<int>(&QComboBox::activated), this, touch);
    connect(m_fitCb,     qOverload<int>(&QComboBox::activated), this, touch);
    connect(m_captionCb, qOverload<int>(&QComboBox::activated), this, touch);
    connect(m_bgCb,      qOverload<int>(&QComboBox::activated), this, touch);
    connect(m_marginSpn, qOverload<void(double)>(&QDoubleSpinBox::valueChanged), this, touch);
    connect(m_gapSpn,    qOverload<void(double)>(&QDoubleSpinBox::valueChanged), this, touch);
    connect(m_captionPt, qOverload<void(int)>(&QSpinBox::valueChanged), this, touch);
    connect(m_grayChk,   &QCheckBox::toggled, this, touch);
    connect(m_borderChk, &QCheckBox::toggled, this, touch);
    for (QRadioButton* r : { m_rAll, m_rOdd, m_rEven, m_rRange })
        connect(r, &QRadioButton::toggled, this, [this]() {
            m_rangeEd->setEnabled(m_rRange->isChecked());
            updateSummary();
        });
    connect(m_rangeEd, &QLineEdit::textEdited, this, [this]() { updateSummary(); });
    connect(m_paperCb, qOverload<int>(&QComboBox::activated), this, [this](int) {
        scheduleRefresh(true);
    });
    connect(m_orientCb, qOverload<int>(&QComboBox::activated), this, [this](int) {
        scheduleRefresh(true);
    });
    connect(m_printerCb, qOverload<int>(&QComboBox::activated), this, [this](int) {
        onPrinterChanged();
    });
    connect(m_prevBtn, &QToolButton::clicked, this, [this]() { goPage(-1); });
    connect(m_nextBtn, &QToolButton::clicked, this, [this]() { goPage(1); });
    connect(btns, &QDialogButtonBox::accepted, this, &PrintDialog::doPrint);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PrintDialog::loadPrinters() {
    AppSettings& st = AppSettings::instance();
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    m_printerCb->blockSignals(true);
    m_printerCb->clear();
    for (const QPrinterInfo& pi : printers) {
        QString tip = pi.makeAndModel();
        if (!pi.location().isEmpty()) tip += (tip.isEmpty() ? "" : " · ") + pi.location();
        const QString label = pi.printerName()
            + (pi.isDefault() ? QString::fromUtf8("  (默认)") : QString());
        m_printerCb->addItem(label, pi.printerName());
        m_printerCb->setItemData(m_printerCb->count() - 1, tip, Qt::ToolTipRole);
    }
    m_printerCb->blockSignals(false);

    if (printers.isEmpty()) {
        m_statusNote = QString::fromUtf8("系统里没有已安装的打印机 —— 装好打印机或加一个"
                                         ""Microsoft Print to PDF"再来。");
        m_copiesLbl->setVisible(false);
        m_copiesSpn->setVisible(false);
        configurePrinter();
        return;
    }
    m_statusNote.clear();
    const QString saved = st.get("Print/printer").toString();
    int i = m_printerCb->findData(saved);
    if (i < 0) {
        const QString def = QPrinterInfo::defaultPrinterName();
        i = m_printerCb->findData(def);
        if (i < 0) i = 0;
    }
    m_printerCb->setCurrentIndex(i);
    onPrinterChanged();
}

void PrintDialog::onPrinterChanged() {
    rebuildPapers();
    scheduleRefresh(true);
}

void PrintDialog::rebuildPapers() {
    AppSettings& st = AppSettings::instance();
    const QPrinterInfo info = QPrinterInfo::printerInfo(m_printerCb->currentData().toString());
    m_sizes = info.supportedPageSizes();
    if (m_sizes.isEmpty()) m_sizes = fallbackPapers();

    // 驱动的默认纸张没在支持列表里时补进最前,否则"默认"根本选不到
    const QPageSize def = info.defaultPageSize();
    if (def.isValid()) {
        bool has = false;
        for (const QPageSize& s : m_sizes) if (s.key() == def.key()) { has = true; break; }
        if (!has) m_sizes.prepend(def);
    }
    const QString savedKey = st.get("Print/paper").toString();
    int want = -1;
    for (int i = 0; i < m_sizes.size(); ++i)
        if (m_sizes[i].key() == savedKey) { want = i; break; }
    if (want < 0)
        for (int i = 0; i < m_sizes.size(); ++i)
            if (m_sizes[i].key() == def.key()) { want = i; break; }
    if (want < 0) want = qMin(0, m_sizes.size() - 1);

    m_paperCb->blockSignals(true);
    m_paperCb->clear();
    for (const QPageSize& s : m_sizes) m_paperCb->addItem(paperLabel(s));
    m_paperCb->setCurrentIndex(want);
    m_paperCb->blockSignals(false);

    // 份数只有驱动支持一次多份时才给 —— 给个不起作用的框是骗人
    const bool multi = info.isNull() ? false : info.supportsCustomPageSizes()
                       && true;   // 占位:真正的判据在下面 configurePrinter 里
    Q_UNUSED(multi);
    if (!m_printer) configurePrinter();
    const bool supportCopies = m_printer && m_printer->supportsMultipleCopies();
    m_copiesLbl->setVisible(supportCopies);
    m_copiesSpn->setVisible(supportCopies);
}

void PrintDialog::configurePrinter() {
    const QPrinterInfo info = QPrinterInfo::printerInfo(m_printerCb->currentData().toString());
    m_printer.reset();
    if (info.isNull()) return;
    m_printer.reset(new QPrinter(info, QPrinter::ScreenResolution));
    const int i = m_paperCb->currentIndex();
    if (i >= 0 && i < m_sizes.size()) m_printer->setPageSize(m_sizes[i]);
    m_printer->setPageOrientation(
        m_orientCb->currentIndex() == 1 ? QPageLayout::Landscape : QPageLayout::Portrait);
    // 注意顺序:换分辨率会重算 pageRect,必须在读几何之前定下来
    m_printer->setResolution(kPrintDpi);
}

PrintOptions PrintDialog::options() const {
    PrintOptions o;
    o.perPage     = cbVal(m_perPageCb, 1);
    o.fit         = cbVal(m_fitCb, PrintFit::Fit);
    o.landscape   = m_orientCb->currentIndex() == 1;
    o.marginMm    = m_marginSpn->value();
    o.gapMm       = m_gapSpn->value();
    o.caption     = cbVal(m_captionCb, PrintCaption::Name);
    o.captionPt   = m_captionPt->value();
    o.grayscale   = m_grayChk->isChecked();
    o.background  = cbVal(m_bgCb, PrintBg::White);
    o.border      = m_borderChk->isChecked();
    return o;
}

// ─────────────────────────────────────────
// 预览
// ─────────────────────────────────────────
void PrintDialog::scheduleRefresh(bool geometryChanged) {
    m_geometryDirty |= geometryChanged;
    if (!m_printing) m_debounce->start();
}

void PrintDialog::goPage(int delta) {
    const int np = qBound(0, m_previewPage + delta, qMax(0, pageCount() - 1));
    if (np == m_previewPage) return;
    m_previewPage = np;
    m_debounce->start();
}

QString PrintDialog::cacheKey(int idx) const {
    const PrintOptions o = options();
    const int want = o.fit == PrintFit::Actual ? 0 : kPrevMaxSide;
    return m_paths.value(idx) + QChar('|') + QString::number(want);
}

void PrintDialog::requestPageDecode() {
    if (!m_printer) { renderPreview(); return; }
    const PrintOptions o = options();
    const int per = std::max(1, o.perPage);
    const int first = m_previewPage * per;
    const int n = qMin(per, m_paths.size() - first);
    const int want = o.fit == PrintFit::Actual ? 0 : kPrevMaxSide;

    QSet<QString> keep;
    for (int i = 0; i < n; ++i)
        keep.insert(m_paths.value(first + i) + QChar('|') + QString::number(want));
    for (auto it = m_cache.begin(); it != m_cache.end(); )
        it = keep.contains(it.key()) ? std::next(it) : m_cache.erase(it);

    m_pending = 0;
    for (int i = 0; i < n; ++i) {
        const int idx = first + i;
        const QString key = m_paths.value(idx) + QChar('|') + QString::number(want);
        if (m_cache.contains(key)) continue;
        ++m_pending;
        const QString path = m_paths.value(idx);
        const bool exif = m_exifRotate;
        QPointer<PrintDialog> self(this);
        // 与查看器同一份解码口径(ImgProc::decodeFull/decodeScaled),
        // 否则"屏幕上看到的"和"纸上的"会在降采样/EXIF 上分叉
        QThreadPool::globalInstance()->start([self, path, key, want, exif]() {
            QImage img = ImgProc::decodeScaled(path, exif, want);
            auto holder = std::make_shared<QImage>(std::move(img));
            QMetaObject::invokeMethod(self, [self, holder, key, want]() {
                if (!self) return;
                self->m_cache.insert(key, PrintDialog::Cached{ want, *holder });
                if (--self->m_pending <= 0) self->renderPreview();
            }, Qt::QueuedConnection);
        });
    }
    if (m_pending == 0) renderPreview();
    else updateSummary();
}

void PrintDialog::renderPreview() {
    if (m_pending > 0 || m_printing) return;      // 等整页解码回来,不留半张页
    if (!m_printer) {
        m_preview->clear();
        updateSummary();
        return;
    }
    const PrintOptions o = options();
    const qreal realDpi = m_printer->resolution() > 0 ? m_printer->resolution() : kPrintDpi;
    const QRectF paper = m_printer->paperRect(QPrinter::DevicePixel);
    const QRectF pageDev = m_printer->pageRect(QPrinter::DevicePixel);
    if (paper.width() < 1 || paper.height() < 1) { updateSummary(); return; }

    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
    const qreal availW = qMax(60.0, m_scroll->viewport()->width()  - 2.0 * kPadDip);
    const qreal availH = qMax(60.0, m_scroll->viewport()->height() - 2.0 * kPadDip);
    // 打印机设备像素 → 屏幕 DIP:纸的物理尺寸不变,只是换一套像素密度
    const qreal k = std::min(availW / (paper.width()  * 96.0 / realDpi),
                             availH / (paper.height() * 96.0 / realDpi));
    const qreal previewDpi = std::clamp(realDpi * k * dpr, 20.0, kPrevDpiMax);
    const qreal f = previewDpi / realDpi;
    const qreal pad = kPadDip * dpr;

    QImage cv(int(paper.width() * f + 2 * pad + 0.5),
              int(paper.height() * f + 2 * pad + 0.5), QImage::Format_RGB32);
    cv.fill(QColor(C_WIN_BG));
    {
        QPainter g(&cv);
        g.fillRect(QRectF(pad, pad, paper.width() * f, paper.height() * f), QColor(255, 255, 255));
        g.translate(pad, pad);
        const QRectF paint(pageDev.x() * f, pageDev.y() * f,
                           pageDev.width() * f, pageDev.height() * f);
        const int want = o.fit == PrintFit::Actual ? 0 : kPrevMaxSide;
        PrintInfoFetcher infoFn = [this, o, want](int idx) {
            PrintImageInfo m;
            const QString path = m_paths.value(idx);
            const QFileInfo fi(path);
            m.name = fi.fileName();
            m.dateText = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
            m.px = ImgProc::orientedSize(path, m_exifRotate);
            m.ok = m.px.width() > 0 && fi.exists();
            if (o.fit == PrintFit::Actual) {   // DPI 只认未降采样解码出来的那张图
                const Cached c = m_cache.value(
                    path + QChar('|') + QString::number(want));
                if (!c.img.isNull() && c.img.dotsPerMeterX() > 0)
                    m.dpiX = c.img.dotsPerMeterX() * 0.0254;
            }
            return m;
        };
        PrintImageFetcher imgFn = [this, want](int idx) {
            return m_cache.value(m_paths.value(idx) + QChar('|') + QString::number(want)).img;
        };
        PrintPageResult res;
        printRenderPage(g, paint, previewDpi, o, m_paths, m_previewPage, infoFn, imgFn, &res);
        m_failedLastPage = res.failed;
        m_shrunkLastPage = res.shrunk;
    }
    cv.setDevicePixelRatio(dpr);
    m_preview->setPixmap(QPixmap::fromImage(cv));
    updateSummary();
}

// ─────────────────────────────────────────
// 页码子集(自己解析、自己翻页,不碰 Qt 的 setFromTo —— 它在 Windows
// 引擎上是驱动翻页还是应用翻页,没有可靠文档,不敢押)
// ─────────────────────────────────────────
QList<int> PrintDialog::pagesToPrint(QString* err) const {
    QList<int> out;
    const int total = pageCount();
    if (total <= 0) return out;
    auto setErr = [err](const QString& s) { if (err) *err = s; };

    if (m_rOdd->isChecked() || m_rEven->isChecked()) {
        const int want = m_rOdd->isChecked() ? 1 : 0;
        for (int p = 1; p <= total; ++p) if (p % 2 == want) out << p - 1;
        return out;
    }
    if (!m_rRange->isChecked()) {
        for (int p = 0; p < total; ++p) out << p;
        return out;
    }

    const QString text = m_rangeEd->text().trimmed();
    if (text.isEmpty()) {
        setErr(QString::fromUtf8("页码框是空的:形如 1-3,5,8-(8- 表示从第 8 页到最后)。"));
        return {};
    }
    QSet<int> seen;
    const QStringList segs = text.split(QRegularExpression("[,，;；、\\s]+"),
                                        Qt::SkipEmptyParts);
    for (const QString& seg : segs) {
        const QStringList ab = seg.split(QLatin1Char('-'), Qt::KeepEmptyParts);
        if (ab.size() > 2) {
            setErr(QString::fromUtf8("页码无法解析:%1").arg(seg));
            return {};
        }
        const QString a = ab.value(0).trimmed(), b = ab.value(1).trimmed();
        bool oka = false, okb = false;
        int from = a.isEmpty() ? 1 : a.toInt(&oka);
        int to   = b.isEmpty() ? (ab.size() > 1 ? total : from) : b.toInt(&okb);
        if (ab.size() == 1) to = from;
        if ((!a.isEmpty() && !oka) || (!b.isEmpty() && !okb) || from < 1 || to < 1) {
            setErr(QString::fromUtf8("页码无法解析:%1").arg(seg));
            return {};
        }
        if (from > to) {
            setErr(QString::fromUtf8("页码 %1 起点大于终点。").arg(seg));
            return {};
        }
        if (from > total || to > total) {
            setErr(QString::fromUtf8("页码 %1 超出总页数 %2。").arg(seg).arg(total));
            return {};
        }
        for (int p = from; p <= to; ++p) seen.insert(p - 1);
    }
    if (seen.isEmpty()) { setErr(QString::fromUtf8("页码条件没命中任何页。")); return {}; }
    out = QList<int>(seen.begin(), seen.end());
    std::sort(out.begin(), out.end());
    return out;
}

int PrintDialog::pageCount() const {
    return printPageCount(m_paths.size(), std::max(1, cbVal(m_perPageCb, 1)));
}

void PrintDialog::updateSummary() {
    const int total = pageCount();
    m_prevBtn->setEnabled(m_previewPage > 0);
    m_nextBtn->setEnabled(m_previewPage + 1 < total);
    m_pageLbl->setText(QString::fromUtf8("第 %1 / %2 页").arg(total ? m_previewPage + 1 : 0).arg(total));

    QString err;
    const QList<int> pages = pagesToPrint(&err);
    QString txt;
    if (!m_statusNote.isEmpty()) {
        txt = m_statusNote;
    } else if (!err.isEmpty()) {
        txt = err;
    } else {
        const PrintOptions o = options();
        txt = QString::fromUtf8("%1 张图片 · 每页 %2 张 · 共 %3 页 · 将打印 %4 页")
                  .arg(m_paths.size()).arg(o.perPage).arg(total).arg(pages.size());
        if (m_copiesSpn->isVisible())
            txt += QString::fromUtf8(" × %1 份").arg(m_copiesSpn->value());
        txt += QString::fromUtf8(" · 纸 %1 %2")
                   .arg(m_paperCb->currentText(),
                        m_orientCb->currentIndex() == 1 ? QString::fromUtf8("横向")
                                                        : QString::fromUtf8("纵向"));
        if (o.fit == PrintFit::Actual)
            txt += QString::fromUtf8("
"原始尺寸"按文件自带 DPI 出图,预览要整张解码,会慢一些。");
        if (m_failedLastPage > 0)
            txt += QString::fromUtf8("
本页 %1 张读不到,已按占格画叉。").arg(m_failedLastPage);
        if (m_shrunkLastPage > 0)
            txt += QString::fromUtf8("
本页 %1 张原始尺寸放不下,已收缩到可印区。").arg(m_shrunkLastPage);
        if (m_pending > 0)
            txt += QString::fromUtf8("
正在取图…");
    }
    m_summary->setText(txt);
    m_printBtn->setEnabled(m_printer && err.isEmpty() && !pages.isEmpty());
}

// ─────────────────────────────────────────
// 出图
// ─────────────────────────────────────────
void PrintDialog::doPrint() {
    QString err;
    const QList<int> pages = pagesToPrint(&err);
    if (!err.isEmpty()) { QMessageBox::warning(this, QString::fromUtf8("打印"), err); return; }
    if (pages.isEmpty()) {
        QMessageBox::warning(this, QString::fromUtf8("打印"),
                             QString::fromUtf8("按当前页码条件没有可打印的页。"));
        return;
    }
    if (m_geometryDirty) { configurePrinter(); m_geometryDirty = false; }
    if (!m_printer) {
        QMessageBox::warning(this, QString::fromUtf8("打印"),
            m_statusNote.isEmpty() ? QString::fromUtf8("打印机未就绪。") : m_statusNote);
        return;
    }
    m_printing = true;
    if (m_debounce) m_debounce->stop();

    const PrintOptions o = options();
    m_printer->setDocName(QString::fromUtf8("Gaze - %1")
                              .arg(QFileInfo(m_paths.first()).absolutePath().toUtf8().isEmpty()
                                       ? QFileInfo(m_paths.first()).fileName()
                                       : QFileInfo(m_paths.first).fileName()));
    if (m_printer->supportsMultipleCopies())
        m_printer->setCopyCount(m_copiesSpn->value());

    const qreal realDpi = m_printer->resolution() > 0 ? m_printer->resolution() : kPrintDpi;
    const QRectF pageDev = m_printer->pageRect(QPrinter::DevicePixel);
    // 出图分辨率 = realDpi,超过可印区像素数的原图对这台打印机没有额外信息量
    const int capSide = int(std::ceil(std::max(pageDev.width(), pageDev.height()))) + 1;

    QProgressDialog prog(QString::fromUtf8("正在准备打印…"), QString::fromUtf8("取消"),
                         0, pages.size(), this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.setMinimumDuration(0);
    prog.setAutoReset(false);
    prog.setValue(0);
    prog.show();
    bool aborted = false;
    connect(&prog, &QProgressDialog::canceled, this, [this]() {
        if (m_printer) m_printer->abort();
    });

    QPainter g;
    if (!g.begin(m_printer.get())) {
        m_printing = false;
        QMessageBox::critical(this, QString::fromUtf8("打印"),
            QString::fromUtf8("无法开始打印作业:打印机拒绝或驱动出错。"));
        updateSummary();
        return;
    }

    // "原始尺寸"档要文件自带 DPI,只能整张解码;画完立刻 take 掉,内存里不同时挂多张原图
    QHash<QString, QImage> fulls;
    PrintInfoFetcher infoFn = [this, o, &fulls](int idx) {
        PrintImageInfo m;
        const QString path = m_paths.value(idx);
        const QFileInfo fi(path);
        m.name = fi.fileName();
        m.dateText = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
        m.px = ImgProc::orientedSize(path, m_exifRotate);
        m.ok = m.px.width() > 0 && fi.exists();
        if (o.fit == PrintFit::Actual) {
            QImage& slot = fulls[path];
            if (slot.isNull()) slot = ImgProc::decodeFull(path, m_exifRotate);
            if (!slot.isNull() && slot.dotsPerMeterX() > 0)
                m.dpiX = slot.dotsPerMeterX() * 0.0254;
        }
        return m;
    };
    PrintImageFetcher imgFn = [this, capSide, &fulls](int idx) {
        const QString path = m_paths.value(idx);
        QImage im = fulls.take(path);
        if (!im.isNull()) return im;
        return ImgProc::decodeScaled(path, m_exifRotate, capSide);
    };

    PrintPageResult all;
    int done = 0;
    for (int i = 0; i < pages.size(); ++i) {
        if (prog.wasCanceled() || m_printer->printerState() == QPrinter::Aborted) {
            aborted = true;
            break;
        }
        prog.setLabelText(QString::fromUtf8("正在打印 第 %1 / %2 页(共 %3 张图)")
                              .arg(i + 1).arg(pages.size())
                              .arg(std::min(std::max(1, o.perPage),
                                            m_paths.size() - pages[i] * std::max(1, o.perPage))));
        PrintPageResult r;
        printRenderPage(g, pageDev, realDpi, o, m_paths, pages[i], infoFn, imgFn, &r);
        fulls.clear();
        all.drawn += r.drawn; all.failed += r.failed; all.shrunk += r.shrunk;
        ++done;
        prog.setValue(done);
        if (i + 1 < pages.size()) g.newPage();
    }
    g.end();
    prog.reset();

    QString txt = aborted ? QString::fromUtf8("已取消:前 %1 页已送印。").arg(done)
        : QString::fromUtf8("已送印 %1 页 · %2 张图片。").arg(done).arg(all.drawn);
    if (all.failed > 0)
        txt += QString::fromUtf8("\n其中 %1 张读不到文件,纸上按占格画了叉。").arg(all.failed);
    if (all.shrunk > 0)
        txt += QString::fromUtf8("\n%1 张按原始尺寸放不下,已收缩。").arg(all.shrunk);
    if (m_printer->printerState() == QPrinter::Error)
        txt += QString::fromUtf8("\n打印机报错(离线/缺纸/拒绝)。");

    m_printing = false;
    persist();
    hide();
    QMessageBox::information(parentWidget() ? parentWidget() : this,
                             QString::fromUtf8("打印"), txt);
    accept();
}

void PrintDialog::persist() {
    AppSettings& st = AppSettings::instance();
    const PrintOptions o = options();
    st.setPersist(QStringLiteral("Print/printer"), m_printerCb->currentData());
    if (m_paperCb->currentIndex() >= 0 && m_paperCb->currentIndex() < m_sizes.size())
        st.setPersist(QStringLiteral("Print/paper"), m_sizes[m_paperCb->currentIndex()].key());
    st.setPersist(QStringLiteral("Print/copies"), m_copiesSpn->value());
    st.setPersist(QStringLiteral("Print/landscape"), o.landscape);
    st.setPersist(QStringLiteral("Print/perPage"), o.perPage);
    st.setPersist(QStringLiteral("Print/fit"), o.fit);
    st.setPersist(QStringLiteral("Print/marginMm"), o.marginMm);
    st.setPersist(QStringLiteral("Print/gapMm"), o.gapMm);
    st.setPersist(QStringLiteral("Print/caption"), o.caption);
    st.setPersist(QStringLiteral("Print/captionPt"), o.captionPt);
    st.setPersist(QStringLiteral("Print/grayscale"), o.grayscale);
    st.setPersist(QStringLiteral("Print/background"), o.background);
    st.setPersist(QStringLiteral("Print/border"), o.border);
    st.setPersist(QStringLiteral("Print/subset"),
                  m_rOdd->isChecked() ? 1 : m_rEven->isChecked() ? 2 : m_rRange->isChecked() ? 3 : 0);
    st.setPersist(QStringLiteral("Print/range"), m_rangeEd->text());
}
