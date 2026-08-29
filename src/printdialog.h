#pragma once
#include <QDialog>
#include <QHash>
#include <QImage>
#include <QPageSize>
#include <QStringList>
#include <QVector>
#include <memory>

#include "printlayout.h"

class QCheckBox;
class QComboBox;
class QDoubleSpinBox;
class QLabel;
class QLineEdit;
class QPrinter;
class QPushButton;
class QRadioButton;
class QResizeEvent;
class QScrollArea;
class QSpinBox;
class QTimer;
class QToolButton;

// ═══════════════════════════════════════════
// 打印对话框。预览与出图共用 printlayout.cpp 里同一个 printRenderPage():
// 预览画在按打印机可印区算出的 QImage 上,出图画在 QPrinter 上,
// 差别只有 paintRect/dpi 与取图器(预览缩到 1024,"原始尺寸"档与出图一律全尺寸)。
// 因此"屏幕上看到的排版"就是"纸上的排版",不存在第二套排版数学。
// ═══════════════════════════════════════════
class PrintDialog : public QDialog {
    Q_OBJECT
public:
    // 唯一入口。candidates 允许混着文件夹/视频等(内部按图像扩展名过滤),
    // 一张图片都没有时给出明确提示 —— 不静默返回。
    static void printImages(QWidget* parent, const QStringList& candidates);

    PrintDialog(QWidget* parent, const QStringList& imagePaths);
    ~PrintDialog() override;

protected:
    void resizeEvent(QResizeEvent* e) override;   // 预览要按新视口重新出图

private:
    struct Cached { int maxSide = -1; QImage img; };   // img 为空 = 解不出来(占位画叉)

    void buildUi();
    void loadPrinters();
    void rebuildPapers();
    void configurePrinter();                 // 用界面值重建 QPrinter(纸张/方向/dpi)
    PrintOptions options() const;

    QString  cacheKey(int idx) const;
    int      previewMaxSide() const;         // "原始尺寸"档=0(全尺寸,DPI 才可信)
    void     requestPageDecode();            // 当前页缺图就丢线程池
    void     renderPreview();
    void     updateSummary();
    QList<int> pagesToPrint(QString* err) const;
    int      pageCount() const;

    void     scheduleRefresh(bool geometryChanged = false);
    void     onPrinterChanged();
    void     goPage(int delta);
    void     doPrint();
    void     persist();

    const QStringList m_paths;

    // 打印机/纸张
    QComboBox*       m_printerCb  = nullptr;
    QComboBox*       m_paperCb    = nullptr;
    QComboBox*       m_orientCb   = nullptr;
    QSpinBox*        m_copiesSpn  = nullptr;
    QLabel*          m_copiesLbl  = nullptr;
    QVector<QPageSize> m_sizes;
    std::unique_ptr<QPrinter> m_printer;
    QString          m_statusNote;           // 无打印机等硬约束,摘要里如实显示

    // 版式
    QComboBox*      m_perPageCb  = nullptr;
    QComboBox*      m_fitCb      = nullptr;
    QDoubleSpinBox* m_marginSpn  = nullptr;
    QDoubleSpinBox* m_gapSpn     = nullptr;
    QComboBox*      m_captionCb  = nullptr;
    QSpinBox*       m_captionPt  = nullptr;
    QComboBox*      m_bgCb       = nullptr;
    QCheckBox*      m_grayChk    = nullptr;
    QCheckBox*      m_borderChk  = nullptr;

    // 页码子集
    QRadioButton* m_rAll   = nullptr;
    QRadioButton* m_rOdd   = nullptr;
    QRadioButton* m_rEven  = nullptr;
    QRadioButton* m_rRange = nullptr;
    QLineEdit*    m_rangeEd = nullptr;

    // 预览
    QScrollArea* m_scroll     = nullptr;
    QLabel*      m_preview    = nullptr;
    QToolButton* m_prevBtn    = nullptr;
    QToolButton* m_nextBtn    = nullptr;
    QLabel*      m_pageLbl    = nullptr;
    QLabel*      m_summary    = nullptr;

    QTimer*  m_debounce       = nullptr;
    int      m_previewPage    = 0;
    int      m_pending        = 0;           // 在途解码数,归零才出图
    int      m_gen            = 0;           // 解码代次:旧批结果回来后一律丢弃
    bool     m_printing       = false;       // 出图中:不许重入(定时器/resize/预览)
    int      m_failedLastPage = 0;
    int      m_shrunkLastPage = 0;
    bool     m_geometryDirty  = false;       // 纸/方向变了要重排(预览尺寸跟着变)
    QHash<QString, Cached> m_cache;
    bool m_exifRotate = true;

    QPushButton* m_printBtn   = nullptr;
};
