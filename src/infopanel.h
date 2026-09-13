#pragma once
// ═══════════════════════════════════════════
// 信息面板:元数据表 + 直方图(任务 #80)
//   来源 @153611「元数据面板,直方图,做,然后放进视图菜单栏里,默认不打开」
//   · 元数据:文件属性一段,EXIF 走 ExifMeta::read(现成的 A/B 两层读取器,
//     标签号已逐个核对过,不在这里重造)。
//   · 直方图:RGB 三通道叠加 + 亮度。缩略图尺寸取样,不开原图(50MP 图不能为画
//     一张直方图去全解码,那是滚动卡顿的老路)。
//   · 全程后台线程算,结果排队回主线程;面板关着时一律不排任务。
// ═══════════════════════════════════════════
#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QLabel>
#include <QFileInfo>
#include <QDateTime>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QPainterPath>
#include <QThreadPool>
#include <QMetaObject>
#include <QPointer>
#include <QVector>
#include <QSettings>
#include <QtMath>
#include "fileentry.h"
#include "exifmeta.h"
#include "settings.h"
#include "wicdecode.h"
#include "imgproc.h"
#include "i18n.h"

class InfoPanel : public QWidget {
public:
    explicit InfoPanel(QWidget* parent = nullptr) : QWidget(parent) {
        auto* root = new QVBoxLayout(this);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        m_hist = new QLabel;
        m_hist->setFixedHeight(110);
        m_hist->setAlignment(Qt::AlignCenter);
        root->addWidget(m_hist);

        m_tree = new QTreeWidget;
        m_tree->setColumnCount(2);
        m_tree->setHeaderLabels({gazeTr("项目"), gazeTr("值")});
        m_tree->setRootIsDecorated(true);
        // #119:关掉隔行换色。开交替色时 Qt 用 palette AlternateBase(白)画偶数行,
        // 与样式表的深色底一起形成"一黑一白"斑马纹 —— 用户要的是整块统一底色。
        m_tree->setAlternatingRowColors(false);
        m_tree->setColumnWidth(0, 150);
        m_tree->setIndentation(14);
        // 样式在应用级 QSS(QLabel#metaHistLabel / QTreeWidget#metaTree,
        // #89 收敛):切主题由 applyLive 重设全局表自动跟上,不再需要重灌钩子
        m_hist->setObjectName(QStringLiteral("metaHistLabel"));
        m_tree->setObjectName(QStringLiteral("metaTree"));
        root->addWidget(m_tree, 1);
    }

    // 换文件:立刻清掉旧内容(避免张冠李戴),再后台算新的
    void showFile(const QString& path) {
        m_path = path;
        m_gen++;
        m_tree->clear();
        m_hist->setPixmap(QPixmap());
        m_hist->setText(QString::fromUtf8("　"));
        if (path.isEmpty()) return;
        if (!isVisible()) return;      // 面板关着:不排任务,只在打开时补算

        const quint64 gen = m_gen;
        const bool wantHist = isImage(path);
        QPointer<InfoPanel> self(this);
        QThreadPool::globalInstance()->start([self, path, gen, wantHist]() {
            // ── 后台:元数据 + 直方图 ──
            const QList<ExifMeta::Field> fields = ExifMeta::read(path);
            QImage thumb;
            if (wantHist) {
                // CMYK JPG(#57):直方图与预览/缩略图共用同一份"WIC 色彩管理"口径,
                // 否则反演图把 B 通道顶高,直方图形状就被一股偏蓝假象带歪
                if (WicDecode::isFourChannelJpeg(path)) {
                    // #206:收口到 decodeCmykCached —— 大图走 4096 副本(512 档
                    // 从副本缩,毫秒级),不再每次选中都全量 WIC 解 4~5 秒
                    thumb = ImgProc::decodeCmykCached(path, 512);
                } else {
                    QImageReader r(path);
                    const QSize orig = r.size();
                    if (orig.isValid() && orig.width() > 0) {
                        // 取样尺寸封顶 512:直方图只要分布,不要细节
                        r.setScaledSize(orig.scaled(512, 512, Qt::KeepAspectRatio));
                        thumb = r.read();
                    }
                }
            }
            // context 传 QPointer:self 已析构时回调直接丢弃;裸 this 会在池
            // 线程解引用悬垂指针(Qt 内部要取 context->thread())
            QMetaObject::invokeMethod(self, [self, path, gen, fields, thumb]() {
                if (!self || gen != self->m_gen) return;   // 期间又换了文件
                if (path != self->m_path) return;
                self->fill(fields, thumb);
            }, Qt::QueuedConnection);
        });
    }

    // 面板从关到开:补算一次(关着时不排任务的兜底)
    void refresh() {
        const QString p = m_path;
        m_path.clear();
        showFile(p);
    }

protected:
    void showEvent(QShowEvent*) override { refresh(); }

private:
    static bool isImage(const QString& path) {
        const QString e = QStringLiteral(".") + QFileInfo(path).suffix().toLower();
        return IMAGE_EXTS.count(e) > 0;
    }

    void fill(const QList<ExifMeta::Field>& fields, const QImage& thumb) {
        m_tree->clear();
        const QFileInfo fi(m_path);

        // ── 第一段:文件属性(永远有内容,EXIF 缺失时面板也不空)──
        auto* file = addGroup(gazeTr("文件"));
        addRow(file, gazeTr("名称"), fi.fileName());
        addRow(file, gazeTr("所在目录"), fi.absolutePath());
        addRow(file, gazeTr("大小"),
               fi.isDir() ? gazeTr("(文件夹)") : formatSize(fi.size()));
        addRow(file, gazeTr("修改时间"), fi.lastModified().toString(
                   QString::fromUtf8("yyyy/MM/dd HH:mm:ss")));
        addRow(file, gazeTr("创建时间"), fi.birthTime().isValid()
                   ? fi.birthTime().toString(QString::fromUtf8("yyyy/MM/dd HH:mm:ss"))
                   : QString::fromUtf8("—"));
        addRow(file, gazeTr("类型"),
               fi.isDir() ? gazeTr("文件夹") : fi.suffix().toUpper());

        if (!thumb.isNull()) {
            auto* img = addGroup(gazeTr("图像"));
            addRow(img, gazeTr("尺寸"),
                   QStringLiteral("%1 × %2").arg(thumb.width()).arg(thumb.height()));
        }

        // ── 第二段:EXIF,按 group 归并 ──
        QString lastGroup;
        QTreeWidgetItem* grp = nullptr;
        for (const ExifMeta::Field& f : fields) {
            if (f.group != lastGroup) {
                lastGroup = f.group;
                grp = addGroup(lastGroup.isEmpty() ? gazeTr("元数据") : lastGroup);
            }
            addRow(grp, f.name, f.value);
        }
        if (fields.isEmpty() && !thumb.isNull())
            addRow(addGroup(gazeTr("元数据")), gazeTr("说明"),
                   gazeTr("该文件没有内嵌 EXIF"));

        // 默认展开"文件"与头两个分组,其余折叠 —— 一屏能看到最常用的
        for (int i = 0; i < m_tree->topLevelItemCount() && i < 2; ++i)
            m_tree->topLevelItem(i)->setExpanded(true);

        if (thumb.isNull()) {
            m_hist->setText(fi.isDir() ? gazeTr("文件夹无直方图")
                                       : gazeTr("无直方图"));
        } else {
            m_hist->setText(QString());
            m_hist->setPixmap(QPixmap::fromImage(drawHistogram(thumb)));
        }
    }

    QTreeWidgetItem* addGroup(const QString& name) {
        auto* it = new QTreeWidgetItem(m_tree);
        it->setText(0, name);
        it->setFirstColumnSpanned(false);
        QFont f = it->font(0);
        f.setBold(true);
        it->setFont(0, f);
        it->setFlags(Qt::ItemIsEnabled);
        m_tree->addTopLevelItem(it);
        return it;
    }
    static void addRow(QTreeWidgetItem* grp, const QString& k, const QString& v) {
        if (!grp) return;
        auto* it = new QTreeWidgetItem(grp);
        it->setText(0, k);
        it->setText(1, v);
        it->setToolTip(1, v);
        it->setFlags(Qt::ItemIsEnabled | Qt::ItemIsSelectable);
    }

    // ── 直方图:RGB 三通道按屏幕混合色叠加 + 亮度白线 ──
    static QImage drawHistogram(const QImage& src) {
        const int W = 256, H = 96;
        QVector<int> r(W, 0), g(W, 0), b(W, 0), l(W, 0);
        const QImage im = src.convertToFormat(QImage::Format_ARGB32);
        const int step = qMax(1, (im.width() * im.height()) / 200000);  // 大图抽样,快
        int n = 0;
        for (int y = 0; y < im.height(); ++y) {
            const QRgb* line = reinterpret_cast<const QRgb*>(im.constScanLine(y));
            for (int x = 0; x < im.width(); x += step) {
                const QRgb p = line[x];
                const int R = qRed(p), G = qGreen(p), B = qBlue(p);
                ++r[R]; ++g[G]; ++b[B];
                // 亮度用 Rec.709
                ++l[qBound(0, int(0.2126 * R + 0.7152 * G + 0.0722 * B), 255)];
                ++n;
            }
        }
        if (n == 0) return {};

        int maxR = 1, maxG = 1, maxB = 1, maxL = 1;
        for (int i = 0; i < W; ++i) {
            maxR = qMax(maxR, r[i]); maxG = qMax(maxG, g[i]);
            maxB = qMax(maxB, b[i]); maxL = qMax(maxL, l[i]);
        }

        QImage out(512, H * 2, QImage::Format_ARGB32);
        // 画布双主题恒深:三通道用 Plus 叠加模式,叠满趋白,浅底上会全被钳成白
        // (同视频 letterbox 的"入库不可回改"逻辑,浅色主题也不改)
        out.fill(QColor(20, 20, 24));
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing, false);
        // 三通道叠加:用 Plus 混合让重叠处接近白,和常见看图软件一致
        p.setCompositionMode(QPainter::CompositionMode_Plus);
        auto draw = [&](const QVector<int>& h, int mx, const QColor& c) {
            QPainterPath path;
            path.moveTo(0, H * 2);
            for (int i = 0; i < W; ++i)
                path.lineTo(i * 2, H * 2 - (double(h[i]) / mx) * (H * 2 - 2));
            path.lineTo(W * 2, H * 2);
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            p.drawPath(path);
        };
        // 用较暗的纯色配 Plus:三通道叠满 ≈ 白
        draw(r, maxR, QColor(110, 0, 0));
        draw(g, maxG, QColor(0, 110, 0));
        draw(b, maxB, QColor(0, 0, 110));

        // 亮度轮廓线(白色细线),叠加模式改回普通
        p.setCompositionMode(QPainter::CompositionMode_SourceOver);
        QPainterPath lp;
        lp.moveTo(0, H * 2 - (double(l[0]) / maxL) * (H * 2 - 2));
        for (int i = 1; i < W; ++i)
            lp.lineTo(i * 2, H * 2 - (double(l[i]) / maxL) * (H * 2 - 2));
        p.setPen(QPen(QColor(235, 235, 240, 200), 1));
        p.setBrush(Qt::NoBrush);
        p.drawPath(lp);
        p.end();
        return out;
    }

    QTreeWidget* m_tree = nullptr;
    QLabel*      m_hist = nullptr;
    QString      m_path;
    quint64      m_gen  = 0;   // 换文件代号,作废在途结果
};
