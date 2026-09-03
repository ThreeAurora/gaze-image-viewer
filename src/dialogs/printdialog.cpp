#include "printdialog.h"
#include "i18n.h"

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
#include <QPixmap>
#include <QPointer>
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
#include <iterator>
#include <memory>

// ═══════════════════════════════════════════
// 这个文件只做三件事:界面 ↔ PrintOptions、QPrinter 几何 ↔ 预览画布、取图器。
// 排版数学一行都不写 —— 全在 printlayout.cpp,预览与出图共用同一个 printRenderPage。
// ═══════════════════════════════════════════

namespace {

constexpr int   kPrintDpi     = 300;    // 出图分辨率。"原始尺寸"档的物理尺寸与它无关
                                        // (scale = dpi/文件 dpi,两个 dpi 会约掉)
constexpr int   kPrevMaxSide  = 1024;   // 预览解码上限;"原始尺寸"档例外(见 cacheKey)
constexpr qreal kPrevDpiMax   = 200;    // 预览画布 dpi 上限(A4 长边 ≈ 2339px,再高纯浪费)
constexpr int   kPadDip       = 12;     // 预览里纸张四周的"桌面"留白

QStringList imageOnly(const QStringList& in) {
    QStringList out;
    for (const QString& p : in)
        if (IMAGE_EXTS.count("." + QFileInfo(p).suffix().toLower())) out << p;
    return out;
}

// 下拉框一律按 data 取值:文案顺序改了不会静默错位(枚举值不靠 index 巧合)
void addCb(QComboBox* cb, const QString& text, int val) { cb->addItem(text, val); }
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
    if (name.isEmpty()) name = gazeTr("自定义");
    return QStringLiteral("%1  %2×%3mm").arg(name)
        .arg(QString::number(mm.width(), 'f', 0), QString::number(mm.height(), 'f', 0));
}

// 驱动一张纸都不报时的兜底清单(残缺驱动/虚拟打印机实测可能给空表)
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
        QMessageBox::information(parent, gazeTr("打印"),
            gazeTr("所选内容里没有可打印的图片(共 %1 项)。")
                .arg(candidates.size()));
        return;
    }
    PrintDialog dlg(parent, imgs);
    dlg.exec();
}

PrintDialog::PrintDialog(QWidget* parent, const QStringList& imagePaths)
    : QDialog(parent), m_paths(imagePaths)
{
    setWindowTitle(gazeTr("打印"));
    resize(1060, 680);
    m_exifRotate = AppSettings::instance().get("General/exifRotate", true).toBool();
    buildUi();

    // 去抖定时器必须先于 loadPrinters 存在:#120 闪退就是这个顺序错了 ——
    // loadPrinters → onPrinterChanged → scheduleRefresh 会用 m_debounce->start(),
    // 而它当时还是 nullptr(本机有打印机才走这条链,所以每次点打印必崩)。
    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    m_debounce->setInterval(120);
    connect(m_debounce, &QTimer::timeout, this, [this]() {
        if (m_printing) return;
        if (m_geometryDirty) { configurePrinter(); m_geometryDirty = false; }
        m_previewPage = qBound(0, m_previewPage, qMax(0, pageCount() - 1));
        requestPageDecode();
    });

    loadPrinters();
    updateSummary();

    m_geometryDirty = true;
    m_debounce->start();   // 首次出图推到事件循环之后:那时视口尺寸才可信
}

PrintDialog::~PrintDialog() = default;

void PrintDialog::resizeEvent(QResizeEvent* e) {
    QDialog::resizeEvent(e);
    if (m_printing || !m_debounce) return;
    m_debounce->start();   // 只是重出图,打印机几何没动
}

// ─────────────────────────────────────────
// 界面
// ─────────────────────────────────────────
void PrintDialog::buildUi() {
    AppSettings& st = AppSettings::instance();

    auto* printerBox = new QGroupBox(gazeTr("打印机"), this);
    {
        auto* f = new QFormLayout(printerBox);
        m_printerCb = new QComboBox(printerBox);
        f->addRow(gazeTr("名称"), m_printerCb);
        m_copiesLbl = new QLabel(gazeTr("份数"), printerBox);
        m_copiesSpn = new QSpinBox(printerBox);
        m_copiesSpn->setRange(1, 99);
        m_copiesSpn->setValue(st.get("Print/copies", 1).toInt());
        m_copiesSpn->setSuffix(gazeTr(" 份"));
        f->addRow(m_copiesLbl, m_copiesSpn);
    }

    auto* pageBox = new QGroupBox(gazeTr("纸张"), this);
    {
        auto* f = new QFormLayout(pageBox);
        m_paperCb = new QComboBox(pageBox);
        f->addRow(gazeTr("纸张"), m_paperCb);
        m_orientCb = new QComboBox(pageBox);
        addCb(m_orientCb, gazeTr("纵向"), 0);
        addCb(m_orientCb, gazeTr("横向"), 1);
        m_orientCb->setCurrentIndex(st.get("Print/landscape", false).toBool() ? 1 : 0);   // #106:landscape 此前只写不读
        f->addRow(gazeTr("方向"), m_orientCb);
        m_marginSpn = new QDoubleSpinBox(pageBox);
        m_marginSpn->setRange(0, 50);
        m_marginSpn->setDecimals(1);
        m_marginSpn->setSingleStep(0.5);
        m_marginSpn->setSuffix(QString::fromUtf8(" mm"));
        m_marginSpn->setValue(st.get("Print/marginMm", 5.0).toDouble());
        f->addRow(gazeTr("页边距"), m_marginSpn);
        m_gapSpn = new QDoubleSpinBox(pageBox);
        m_gapSpn->setRange(0, 30);
        m_gapSpn->setDecimals(1);
        m_gapSpn->setSingleStep(0.5);
        m_gapSpn->setSuffix(QString::fromUtf8(" mm"));
        m_gapSpn->setValue(st.get("Print/gapMm", 2.0).toDouble());
        f->addRow(gazeTr("图间距"), m_gapSpn);
    }

    auto* layBox = new QGroupBox(gazeTr("版式"), this);
    {
        auto* f = new QFormLayout(layBox);
        m_perPageCb = new QComboBox(layBox);
        for (int n : {1, 2, 3, 4, 6, 9})
            addCb(m_perPageCb, gazeTr("%1 张/页").arg(n), n);
        cbSetVal(m_perPageCb, st.get("Print/perPage", 1).toInt());
        f->addRow(gazeTr("每页"), m_perPageCb);

        m_fitCb = new QComboBox(layBox);
        addCb(m_fitCb, gazeTr("适应边框"), PrintFit::Fit);
        addCb(m_fitCb, gazeTr("不放大"),   PrintFit::NoUpscale);
        addCb(m_fitCb, gazeTr("原始尺寸"), PrintFit::Actual);
        addCb(m_fitCb, gazeTr("填满裁边"), PrintFit::Fill);
        cbSetVal(m_fitCb, st.get("Print/fit", PrintFit::Fit).toInt());
        f->addRow(gazeTr("缩放"), m_fitCb);

        m_captionCb = new QComboBox(layBox);
        addCb(m_captionCb, gazeTr("无"),         PrintCaption::None);
        addCb(m_captionCb, gazeTr("文件名"),      PrintCaption::Name);
        addCb(m_captionCb, gazeTr("文件名+尺寸"), PrintCaption::NameSize);
        addCb(m_captionCb, gazeTr("文件名+日期"), PrintCaption::NameDate);
        cbSetVal(m_captionCb, st.get("Print/caption", PrintCaption::Name).toInt());
        f->addRow(gazeTr("说明文字"), m_captionCb);

        m_captionPt = new QSpinBox(layBox);
        m_captionPt->setRange(6, 24);
        m_captionPt->setValue(st.get("Print/captionPt", 9).toInt());
        m_captionPt->setSuffix(QString::fromUtf8(" pt"));
        f->addRow(gazeTr("文字字号"), m_captionPt);

        m_bgCb = new QComboBox(layBox);
        addCb(m_bgCb, gazeTr("白色"),   PrintBg::White);
        addCb(m_bgCb, gazeTr("黑色"),   PrintBg::Black);
        addCb(m_bgCb, gazeTr("不填充"), PrintBg::None);
        cbSetVal(m_bgCb, st.get("Print/background", PrintBg::White).toInt());
        f->addRow(gazeTr("背景"), m_bgCb);

        m_grayChk = new QCheckBox(gazeTr("转灰度"), layBox);
        m_grayChk->setChecked(st.get("Print/grayscale", false).toBool());
        m_borderChk = new QCheckBox(gazeTr("每张图描边"), layBox);
        m_borderChk->setChecked(st.get("Print/border", false).toBool());
        auto* chk = new QHBoxLayout();
        chk->addWidget(m_grayChk);
        chk->addWidget(m_borderChk);
        f->addRow(QString(), chk);
    }

    auto* rangeBox = new QGroupBox(gazeTr("打印范围"), this);
    {
        auto* g = new QGridLayout(rangeBox);
        g->setContentsMargins(9, 6, 9, 6);
        m_rAll   = new QRadioButton(gazeTr("全部"), rangeBox);
        m_rOdd   = new QRadioButton(gazeTr("奇数页"), rangeBox);
        m_rEven  = new QRadioButton(gazeTr("偶数页"), rangeBox);
        m_rRange = new QRadioButton(gazeTr("页码"), rangeBox);
        m_rangeEd = new QLineEdit(rangeBox);
        m_rangeEd->setPlaceholderText(QStringLiteral("1-3,5,8-"));
        m_rangeEd->setEnabled(false);
        const int subset = st.get("Print/subset", 0).toInt();
        (subset == 1 ? m_rOdd : subset == 2 ? m_rEven : subset == 3 ? m_rRange : m_rAll)
            ->setChecked(true);
        m_rangeEd->setText(st.get("Print/range").toString());
        g->addWidget(m_rAll,    0, 0, 1, 2);
        g->addWidget(m_rOdd,    1, 0, 1, 2);
        g->addWidget(m_rEven,   2, 0, 1, 2);
        g->addWidget(m_rRange,  3, 0);
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

    m_prevBtn = new QToolButton(this);
    m_prevBtn->setArrowType(Qt::LeftArrow);
    m_prevBtn->setToolTip(gazeTr("上一页"));
    m_nextBtn = new QToolButton(this);
    m_nextBtn->setArrowType(Qt::RightArrow);
    m_nextBtn->setToolTip(gazeTr("下一页"));
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
    m_summary->setTextFormat(Qt::PlainText);

    auto* right = new QVBoxLayout();
    right->addLayout(nav);
    right->addWidget(m_scroll, 1);
    right->addWidget(m_summary);

    auto* top = new QHBoxLayout();
    top->addWidget(leftWrap);
    top->addLayout(right, 1);

    auto* btns = new QDialogButtonBox(Qt::Horizontal, this);
    m_printBtn = btns->addButton(gazeTr("打印"), QDialogButtonBox::AcceptRole);
    m_printBtn->setDefault(true);
    btns->addButton(QDialogButtonBox::Cancel);

    auto* outer = new QVBoxLayout(this);
    outer->addLayout(top, 1);
    outer->addWidget(btns);

    // ── 接线:版式改动 → 去抖重出图 ──
    auto touch = [this]() { scheduleRefresh(); };
    connect(m_perPageCb, &QComboBox::activated, this, touch);
    connect(m_fitCb,     &QComboBox::activated, this, touch);
    connect(m_captionCb, &QComboBox::activated, this, touch);
    connect(m_bgCb,      &QComboBox::activated, this, touch);
    connect(m_marginSpn, &QDoubleSpinBox::valueChanged, this, touch);
    connect(m_gapSpn,    &QDoubleSpinBox::valueChanged, this, touch);
    connect(m_captionPt, &QSpinBox::valueChanged, this, touch);
    connect(m_grayChk,   &QCheckBox::toggled, this, touch);
    connect(m_borderChk, &QCheckBox::toggled, this, touch);
    for (QRadioButton* r : { m_rAll, m_rOdd, m_rEven, m_rRange })
        connect(r, &QRadioButton::toggled, this, [this]() {
            m_rangeEd->setEnabled(m_rRange->isChecked());
            updateSummary();
        });
    connect(m_rangeEd, &QLineEdit::textEdited, this, [this]() { updateSummary(); });
    connect(m_paperCb,  &QComboBox::activated, this, [this]() { scheduleRefresh(true); });
    connect(m_orientCb, &QComboBox::activated, this, [this]() { scheduleRefresh(true); });
    connect(m_copiesSpn, &QSpinBox::valueChanged, this, [this]() { updateSummary(); });
    connect(m_printerCb, &QComboBox::activated, this, [this]() { onPrinterChanged(); });
    connect(m_prevBtn, &QToolButton::clicked, this, [this]() { goPage(-1); });
    connect(m_nextBtn, &QToolButton::clicked, this, [this]() { goPage(1); });
    connect(btns, &QDialogButtonBox::accepted, this, &PrintDialog::doPrint);
    connect(btns, &QDialogButtonBox::rejected, this, &QDialog::reject);
}

void PrintDialog::loadPrinters() {
    const QList<QPrinterInfo> printers = QPrinterInfo::availablePrinters();
    m_printerCb->blockSignals(true);
    m_printerCb->clear();
    for (const QPrinterInfo& pi : printers) {
        QString tip = pi.makeAndModel();
        if (!pi.location().isEmpty()) tip += (tip.isEmpty() ? QString() : QStringLiteral(" · "))
                                            + pi.location();
        const QString label = pi.printerName()
            + (pi.isDefault() ? gazeTr("  (默认)") : QString());
        m_printerCb->addItem(label, pi.printerName());
        m_printerCb->setItemData(m_printerCb->count() - 1, tip, Qt::ToolTipRole);
    }
    m_printerCb->blockSignals(false);

    if (printers.isEmpty()) {
        m_statusNote = gazeTr(
            "系统里没有已安装的打印机:装好驱动,或添加一个\"Microsoft Print to PDF\"再来。");
        m_copiesLbl->setVisible(false);
        m_copiesSpn->setVisible(false);
        configurePrinter();
        return;
    }
    m_statusNote.clear();
    int i = m_printerCb->findData(AppSettings::instance().get("Print/printer").toString());
    if (i < 0) i = m_printerCb->findData(QPrinterInfo::defaultPrinterName());
    m_printerCb->setCurrentIndex(i < 0 ? 0 : i);
    onPrinterChanged();
}

void PrintDialog::onPrinterChanged() {
    rebuildPapers();       // 先填纸张表(几何要按选中的纸重建)
    configurePrinter();    // 再建 QPrinter:份数支持与否只有它知道
    const bool multi = m_printer && m_printer->supportsMultipleCopies();
    m_copiesLbl->setVisible(multi);
    m_copiesSpn->setVisible(multi);
    scheduleRefresh(true);
}

void PrintDialog::rebuildPapers() {
    const QPrinterInfo info = QPrinterInfo::printerInfo(m_printerCb->currentData().toString());
    m_sizes = info.supportedPageSizes();
    if (m_sizes.isEmpty()) m_sizes = fallbackPapers();

    // 驱动的默认纸张不在支持列表里时补到最前,否则"默认"根本选不到
    const QPageSize def = info.defaultPageSize();
    if (def.isValid()) {
        bool has = false;
        for (const QPageSize& s : m_sizes) if (s.key() == def.key()) { has = true; break; }
        if (!has) m_sizes.prepend(def);
    }
    const QString savedKey = AppSettings::instance().get("Print/paper").toString();
    int want = -1;
    for (int i = 0; i < m_sizes.size(); ++i)
        if (m_sizes[i].key() == savedKey) { want = i; break; }
    if (want < 0)
        for (int i = 0; i < m_sizes.size(); ++i)
            if (m_sizes[i].key() == def.key()) { want = i; break; }
    if (want < 0) want = 0;

    m_paperCb->blockSignals(true);
    m_paperCb->clear();
    for (const QPageSize& s : m_sizes) m_paperCb->addItem(paperLabel(s));
    m_paperCb->setCurrentIndex(qMin(want, int(m_sizes.size()) - 1));
    m_paperCb->blockSignals(false);
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
    // 顺序要紧:分辨率定了 pageRect 才是最终值,几何全在它之后读
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
    // 兜底:构造期任何新增的调用路径都不该再撞到空定时器(见 ctor 的顺序注释)
    if (!m_printing && m_debounce) m_debounce->start();
}

void PrintDialog::goPage(int delta) {
    const int np = qBound(0, m_previewPage + delta, qMax(0, pageCount() - 1));
    if (np == m_previewPage) return;
    m_previewPage = np;
    if (!m_printing) m_debounce->start();
}

QString PrintDialog::cacheKey(int idx) const {
    return m_paths.value(idx) + QChar('|') + QString::number(previewMaxSide());
}

// "原始尺寸"档必须全尺寸解码:DPI 只能从没降采样的解码里拿
// (实测 Qt6.5.3 —— JPEG 缩到 1/6.67 后 dotsPerMeter 仍是 300,PNG 同比例会变成 45)
int PrintDialog::previewMaxSide() const {
    return cbVal(m_fitCb, PrintFit::Fit) == PrintFit::Actual ? 0 : kPrevMaxSide;
}

void PrintDialog::requestPageDecode() {
    if (!m_printer) { renderPreview(); return; }
    const PrintOptions o = options();
    const int per = std::max(1, o.perPage);
    const int first = m_previewPage * per;
    const int n = qMin(per, m_paths.size() - first);
    const int want = previewMaxSide();

    // 只留本页需要的:翻页/改选项后旧图不再占内存(整页全尺寸最多 9 张)
    QSet<QString> keep;
    for (int i = 0; i < n; ++i) keep.insert(cacheKey(first + i));
    for (auto it = m_cache.begin(); it != m_cache.end(); )
        it = keep.contains(it.key()) ? std::next(it) : m_cache.erase(it);

    ++m_gen;                       // 上一批在途解码的结果就此作废(见下面 gen 判断)
    const int gen = m_gen;
    m_pending = 0;
    for (int i = 0; i < n; ++i) {
        const int idx = first + i;
        const QString key = cacheKey(idx);
        if (m_cache.contains(key)) continue;
        ++m_pending;
        const QString path = m_paths.value(idx);
        const bool exif = m_exifRotate;
        QPointer<PrintDialog> self(this);
        // 解码口径与查看器同一个 ImgProc,预览/出图/屏幕不会分叉
        QThreadPool::globalInstance()->start([self, path, key, want, exif, gen]() {
            QImage img = ImgProc::decodeScaled(path, exif, want);
            auto holder = std::make_shared<QImage>(std::move(img));
            QMetaObject::invokeMethod(self, [self, holder, key, want, gen]() {
                if (!self || self->m_gen != gen) return;
                self->m_cache.insert(key, PrintDialog::Cached{ want, *holder });
                if (--self->m_pending == 0) self->renderPreview();
            }, Qt::QueuedConnection);
        });
    }
    if (m_pending == 0) renderPreview();
    else updateSummary();
}

void PrintDialog::renderPreview() {
    if (m_pending > 0 || m_printing) return;      // 等整页解码回来,不画半张页
    if (!m_printer) {
        m_preview->clear();
        updateSummary();
        return;
    }
    const PrintOptions o = options();
    const qreal realDpi = m_printer->resolution() > 0 ? m_printer->resolution() : kPrintDpi;
    const QRectF paper   = m_printer->paperRect(QPrinter::DevicePixel);
    const QRectF pageDev = m_printer->pageRect(QPrinter::DevicePixel);
    if (paper.width() < 1 || paper.height() < 1) { updateSummary(); return; }

    const qreal dpr = devicePixelRatioF() > 0 ? devicePixelRatioF() : 1.0;
    const qreal availW = qMax(60.0, double(m_scroll->viewport()->width())  - 2.0 * kPadDip);
    const qreal availH = qMax(60.0, double(m_scroll->viewport()->height()) - 2.0 * kPadDip);
    // 同一张纸换一套像素密度:打印机设备像素 → 画布设备像素
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
        // 先铺"纸",再画可印区:印不到的那一圈留白,和真纸上看到的一致
        g.fillRect(QRectF(pad, pad, paper.width() * f, paper.height() * f), QColor(255, 255, 255));
        g.translate(pad, pad);
        const QRectF paint(pageDev.x() * f, pageDev.y() * f,
                           pageDev.width() * f, pageDev.height() * f);
        PrintInfoFetcher infoFn = [this, o](int idx) {
            PrintImageInfo m;
            const QString path = m_paths.value(idx);
            const QFileInfo fi(path);
            m.name = fi.fileName();
            m.dateText = fi.lastModified().toString(QStringLiteral("yyyy-MM-dd"));
            m.px = ImgProc::orientedSize(path, m_exifRotate);
            m.ok = m.px.width() > 0 && fi.exists();
            if (o.fit == PrintFit::Actual) {
                const Cached c = m_cache.value(cacheKey(idx));
                if (!c.img.isNull() && c.img.dotsPerMeterX() > 0)
                    m.dpiX = c.img.dotsPerMeterX() * 0.0254;
            }
            return m;
        };
        PrintImageFetcher imgFn = [this](int idx) {
            return m_cache.value(cacheKey(idx)).img;   // 缺图=还没解出来,整页会晚点再画
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
// 页码子集:自己解析、自己翻页。Qt 的 setFromTo/setPrintRange 在 Windows 引擎上
// 到底是驱动翻页还是应用翻页,没有可靠说法,不敢押。
// ─────────────────────────────────────────
QList<int> PrintDialog::pagesToPrint(QString* err) const {
    QList<int> out;
    const int total = pageCount();
    if (total <= 0) return out;
    auto bad = [err, &out](const QString& s) { if (err) *err = s; out.clear(); return out; };

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
    if (text.isEmpty())
        return bad(gazeTr("页码框是空的:形如 1-3,5,8-(8- 表示从第 8 页到最后)。"));

    QSet<int> seen;
    const QStringList segs = text.split(QRegularExpression(QStringLiteral("[,，;；、\\s]+")),
                                        Qt::SkipEmptyParts);
    if (segs.isEmpty())
        return bad(gazeTr("页码无法解析:%1").arg(text));
    for (const QString& seg : segs) {
        const QStringList ab = seg.split(QLatin1Char('-'), Qt::KeepEmptyParts);
        if (ab.size() > 2) return bad(gazeTr("页码无法解析:%1").arg(seg));
        const QString a = ab.value(0).trimmed(), b = ab.value(1).trimmed();
        if (a.isEmpty() && b.isEmpty()) return bad(gazeTr("页码无法解析:%1").arg(seg));
        bool oka = false, okb = false;
        const int from = a.isEmpty() ? 1            : a.toInt(&oka);
        const int to   = b.isEmpty() ? (ab.size() > 1 ? total : from) : b.toInt(&okb);
        if ((!a.isEmpty() && !oka) || (!b.isEmpty() && !okb))
            return bad(gazeTr("页码无法解析:%1").arg(seg));
        if (from < 1 || to < 1)
            return bad(gazeTr("页码要从 1 开始:%1").arg(seg));
        if (from > to)
            return bad(gazeTr("页码 %1 起点大于终点。").arg(seg));
        if (to > total)
            return bad(gazeTr("页码 %1 超出总页数 %2。").arg(seg).arg(total));
        for (int p = from; p <= to; ++p) seen.insert(p - 1);
    }
    if (seen.isEmpty()) return bad(gazeTr("页码条件没命中任何页。"));
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
    m_pageLbl->setText(gazeTr("第 %1 / %2 页")
                           .arg(total ? m_previewPage + 1 : 0).arg(total));

    QString err;
    const QList<int> pages = pagesToPrint(&err);
    QString txt;
    if (!m_statusNote.isEmpty()) {
        txt = m_statusNote;
    } else if (!err.isEmpty()) {
        txt = err;
    } else {
        const PrintOptions o = options();
        txt = gazeTr("%1 张图片 · 每页 %2 张 · 共 %3 页 · 将打印 %4 页")
                  .arg(m_paths.size()).arg(o.perPage).arg(total).arg(pages.size());
        if (!m_copiesSpn->isHidden())
            txt += gazeTr(" × %1 份").arg(m_copiesSpn->value());
        txt += gazeTr(" · %1 %2")
                   .arg(m_paperCb->currentText(),
                        m_orientCb->currentIndex() == 1 ? gazeTr("横向")
                                                        : gazeTr("纵向"));
        if (o.fit == PrintFit::Actual)
            txt += gazeTr("\n\"原始尺寸\"按文件自带 DPI 出图:预览要把整张图解开,会慢一些。");
        if (m_failedLastPage > 0)
            txt += gazeTr("\n本页 %1 张读不到文件,已按占格画叉。").arg(m_failedLastPage);
        if (m_shrunkLastPage > 0)
            txt += gazeTr("\n本页 %1 张按原始尺寸放不下,已收缩到可印区。").arg(m_shrunkLastPage);
        if (m_pending > 0) txt += gazeTr("\n正在取图…");
    }
    m_summary->setText(txt);
    m_printBtn->setEnabled(m_printer != nullptr && err.isEmpty() && !pages.isEmpty());
}

// ─────────────────────────────────────────
// 出图
// ─────────────────────────────────────────
void PrintDialog::doPrint() {
    QString err;
    const QList<int> pages = pagesToPrint(&err);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, gazeTr("打印"), err);
        return;
    }
    if (pages.isEmpty()) {
        QMessageBox::warning(this, gazeTr("打印"),
                             gazeTr("按当前页码条件没有可打印的页。"));
        return;
    }
    if (m_geometryDirty) { configurePrinter(); m_geometryDirty = false; }
    if (!m_printer) {
        QMessageBox::warning(this, gazeTr("打印"),
            m_statusNote.isEmpty() ? gazeTr("打印机未就绪。") : m_statusNote);
        return;
    }
    m_printing = true;
    if (m_debounce) m_debounce->stop();

    const PrintOptions o = options();
    const QFileInfo first(m_paths.first());
    m_printer->setDocName(gazeTr("Gaze - %1")
                              .arg(QFileInfo(first.absolutePath()).fileName()));
    if (m_printer->supportsMultipleCopies())
        m_printer->setCopyCount(m_copiesSpn->value());

    const qreal realDpi = m_printer->resolution() > 0 ? m_printer->resolution() : kPrintDpi;
    const QRectF pageDev = m_printer->pageRect(QPrinter::DevicePixel);
    // 出图就是 realDpi 分辨率:比可印区像素还多的原图,对这台打印机没有额外信息量
    const int capSide = int(std::ceil(std::max(pageDev.width(), pageDev.height()))) + 1;

    QProgressDialog prog(gazeTr("正在准备打印…"), gazeTr("取消"),
                         0, pages.size(), this);
    prog.setWindowModality(Qt::ApplicationModal);
    prog.setMinimumDuration(0);
    prog.show();
    connect(&prog, &QProgressDialog::canceled, this,
            [this]() { if (m_printer) m_printer->abort(); });

    QPainter g;
    if (!g.begin(m_printer.get())) {
        m_printing = false;
        QMessageBox::critical(this, gazeTr("打印"),
            gazeTr("无法开始打印作业:打印机拒绝或驱动出错。"));
        updateSummary();
        return;
    }

    // "原始尺寸"档要文件自带 DPI,只能整张解码;画完立刻 take 走,不同时在内存里挂多张原图
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
        if (QImage im = fulls.take(path); !im.isNull()) return im;
        return ImgProc::decodeScaled(path, m_exifRotate, capSide);
    };

    PrintPageResult all;
    int done = 0;
    bool aborted = false;
    for (int i = 0; i < pages.size(); ++i) {
        if (prog.wasCanceled() || m_printer->printerState() == QPrinter::Aborted) {
            aborted = true;
            break;
        }
        prog.setLabelText(gazeTr("正在打印 第 %1 / %2 页").arg(i + 1).arg(pages.size()));
        PrintPageResult r;
        printRenderPage(g, pageDev, realDpi, o, m_paths, pages[i], infoFn, imgFn, &r);
        fulls.clear();
        all.drawn += r.drawn; all.failed += r.failed; all.shrunk += r.shrunk;
        ++done;
        prog.setValue(done);
        if (i + 1 < pages.size()) m_printer->newPage();   // Qt6:QPainter 不再有 newPage()
    }
    g.end();
    prog.setValue(pages.size());   // 到达 maximum 时 autoClose 会收起;取消时靠下面这行
    prog.hide();
    const bool jobError = m_printer->printerState() == QPrinter::Error;

    QString txt = aborted ? gazeTr("已取消:前 %1 页已送印。").arg(done)
                          : gazeTr("已送印 %1 页 · %2 张图片。").arg(done).arg(all.drawn);
    if (all.failed > 0)
        txt += gazeTr("\n其中 %1 张读不到文件,纸上已按占格画叉。").arg(all.failed);
    if (all.shrunk > 0)
        txt += gazeTr("\n%1 张按原始尺寸放不下,已收缩。").arg(all.shrunk);
    if (jobError)
        txt += gazeTr("\n打印机报错(离线/缺纸/拒绝)。");

    m_printing = false;
    persist();
    hide();
    QMessageBox::information(parentWidget() ? parentWidget() : static_cast<QWidget*>(this),
                             gazeTr("打印"), txt);
    accept();
}

void PrintDialog::persist() {
    AppSettings& st = AppSettings::instance();
    const PrintOptions o = options();
    st.setPersist(QStringLiteral("Print/printer"), m_printerCb->currentData());
    const int pi = m_paperCb->currentIndex();
    if (pi >= 0 && pi < m_sizes.size())
        st.setPersist(QStringLiteral("Print/paper"), m_sizes[pi].key());
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
                  m_rOdd->isChecked() ? 1 : m_rEven->isChecked() ? 2
                                                                : m_rRange->isChecked() ? 3 : 0);
    st.setPersist(QStringLiteral("Print/range"), m_rangeEd->text());
}
