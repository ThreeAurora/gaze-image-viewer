#pragma once
// ═══════════════════════════════════════════
// 无损裁剪对话框(#83)
//   交互:在缩放预览上拖框选区 → 松开即吸附到"无损边界" → 回车/确定执行。
//   无损边界:JPEG 以 MCU(宏块)为单位存像素,-crop 的起点与尺寸若不是 MCU
//   的整数倍,边缘块必须重新编码 → 那部分是有损的。常见采样下 MCU 为 8(4:4:4)
//   或 16(4:2:0/4:2:2),这里统一吸附到 16,配合 jpegtran -perfect 校验:
//   -perfect 会在无法无损完成时直接失败,失败就如实告诉用户,不偷偷降质。
// ═══════════════════════════════════════════
#include <QDialog>
#include <QLabel>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPushButton>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>
#include <QFileInfo>
#include <QSettings>
#include <QKeySequence>
#include <QShortcut>
#include <algorithm>
#include <cmath>
#include "i18n.h"
#include "constants.h"

// 刻意不加 Q_OBJECT:这是纯头文件类,不进 AUTOMOC(加了会缺 vtable)。
// 本类不声明任何信号/槽/属性,连的都是 QDialog 已有的槽,不需要元对象。
class CropDialog : public QDialog {
public:
    CropDialog(const QString& path, QWidget* parent = nullptr)
        : QDialog(parent), m_path(path)
    {
        setWindowTitle(gazeTr("裁剪 — %1").arg(QFileInfo(path).fileName()));
        setModal(true);
        resize(920, 660);

        m_img.load(path);

        auto* root = new QVBoxLayout(this);
        m_view = new QLabel;
        m_view->setObjectName(QStringLiteral("cropView"));
        m_view->setAlignment(Qt::AlignCenter);
        m_view->setMinimumSize(400, 300);
        m_view->setCursor(Qt::CrossCursor);
        root->addWidget(m_view, 1);

        m_info = new QLabel;
        m_info->setObjectName(QStringLiteral("cropInfo"));
        root->addWidget(m_info);

        auto* bar = new QHBoxLayout;
        m_btnFull = new QPushButton(gazeTr("全选"));
        m_btnOk   = new QPushButton(gazeTr("裁剪"));
        auto* cancel = new QPushButton(gazeTr("取消"));
        m_btnOk->setDefault(true);
        bar->addWidget(m_btnFull);
        bar->addStretch();
        bar->addWidget(cancel);
        bar->addWidget(m_btnOk);
        root->addLayout(bar);

        connect(m_btnFull, &QPushButton::clicked, this, [this]() {
            m_sel = QRect(0, 0, m_img.width(), m_img.height());
            snapSel(); updateView();
        });
        connect(cancel, &QPushButton::clicked, this, &QDialog::reject);
        connect(m_btnOk, &QPushButton::clicked, this, &QDialog::accept);
        auto* esc = new QShortcut(QKeySequence(Qt::Key_Escape), this);
        connect(esc, &QShortcut::activated, this, [this]() { reject(); });

        // 初始给个居中 80% 的默认选区,省得用户从零开始拖
        const int w = m_img.width() * 4 / 5, h = m_img.height() * 4 / 5;
        m_sel = QRect((m_img.width() - w) / 2, (m_img.height() - h) / 2, w, h);
        snapSel();
        m_view->installEventFilter(this);
    }

    QRect cropRect() const { return m_sel; }
    static constexpr int MCU = 16;   // 无损吸附粒度

protected:
    void resizeEvent(QResizeEvent*) override { updateView(); }

    // 2026-09-05 用户令:框选只有第一次有效 —— 第一次拖拽确定裁剪窗口之后,
    // 光标就只被允许「移动窗口」(按住窗口内部)或「调整窗口大小」(按住边/角),
    // 不能再重新框选。窗口内/边/角的命中分区见 gripAt。
    enum Grip { GripNone, GripSelect, GripMove,
                GripTL, GripT, GripTR, GripR, GripBR, GripB, GripBL, GripL };

    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (obj != m_view) return QDialog::eventFilter(obj, ev);
        switch (ev->type()) {
        case QEvent::MouseButtonPress: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (me->button() != Qt::LeftButton) break;
            if (!m_established) {           // 第一次:框选(此后不再有框选)
                m_grip = GripSelect;
                m_dragging = true;
                m_anchor = toImage(me->pos());
                m_sel = QRect(m_anchor, QSize(1, 1));
            } else {
                m_grip = gripAt(me->pos());
                if (m_grip == GripNone) return true;   // 窗口之外:不动作
                m_dragging = true;
                m_pressView = me->pos();
                m_origSel = m_sel;
            }
            updateView();
            return true;
        }
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(ev);
            if (!m_dragging) { updateCursor(me->pos()); break; }
            switch (m_grip) {
            case GripSelect: {
                const QPoint cur = toImage(me->pos());
                m_sel = QRect(std::min(m_anchor.x(), cur.x()), std::min(m_anchor.y(), cur.y()),
                              std::abs(cur.x() - m_anchor.x()), std::abs(cur.y() - m_anchor.y()));
                break;
            }
            case GripMove: {
                const double sc = qMax(0.0001, m_scale);
                QPoint d = me->pos() - m_pressView;
                int nx = m_origSel.x() + int(std::round(d.x() / sc));
                int ny = m_origSel.y() + int(std::round(d.y() / sc));
                nx = qBound(0, nx, m_img.width()  - m_origSel.width());
                ny = qBound(0, ny, m_img.height() - m_origSel.height());
                m_sel.moveTo(nx, ny);
                break;
            }
            default: {   // 边/角缩放:固定对角(对边),另一端跟手,钳在图内
                const QPoint cur = toImage(me->pos());
                int x0 = m_origSel.x(), y0 = m_origSel.y();
                int x1 = m_origSel.right(), y1 = m_origSel.bottom();
                if (m_grip == GripTL || m_grip == GripT || m_grip == GripTR) y0 = cur.y();
                if (m_grip == GripTL || m_grip == GripL || m_grip == GripBL) x0 = cur.x();
                if (m_grip == GripBR || m_grip == GripB || m_grip == GripBL) y1 = cur.y();
                if (m_grip == GripBR || m_grip == GripR || m_grip == GripTR) x1 = cur.x();
                m_sel = QRect(QPoint(std::min(x0, x1), std::min(y0, y1)),
                              QPoint(std::max(x0, x1), std::max(y0, y1)));
                m_sel = m_sel.intersected(QRect(0, 0, m_img.width(), m_img.height()));
                break;
            }
            }
            updateView();
            return true;
        }
        case QEvent::MouseButtonRelease: {
            if (!m_dragging) break;
            m_dragging = false;
            snapSel();
            m_established = true;
            updateView();
            return true;
        }
        default: break;
        }
        return QDialog::eventFilter(obj, ev);
    }

private:
    // 选区在视图坐标下的矩形(toImage 的逆)
    QRect viewRect() const {
        const int ox = (m_view->width()  - int(m_img.width()  * m_scale)) / 2;
        const int oy = (m_view->height() - int(m_img.height() * m_scale)) / 2;
        return QRect(ox + int(m_sel.x() * m_scale), oy + int(m_sel.y() * m_scale),
                     int(m_sel.width() * m_scale), int(m_sel.height() * m_scale));
    }
    // 命中分区:边/角 6px 带 = 调整大小,内部 = 移动,外部 = 无动作
    Grip gripAt(const QPoint& viewPos) const {
        const QRect r = viewRect();
        const int pad = 6;
        const bool L = std::abs(viewPos.x() - r.left())   <= pad;
        const bool R = std::abs(viewPos.x() - r.right())  <= pad;
        const bool T = std::abs(viewPos.y() - r.top())    <= pad;
        const bool B = std::abs(viewPos.y() - r.bottom()) <= pad;
        if (L && T) return GripTL;
        if (R && T) return GripTR;
        if (L && B) return GripBL;
        if (R && B) return GripBR;
        if (T && viewPos.x() > r.left() && viewPos.x() < r.right()) return GripT;
        if (B && viewPos.x() > r.left() && viewPos.x() < r.right()) return GripB;
        if (L && viewPos.y() > r.top() && viewPos.y() < r.bottom()) return GripL;
        if (R && viewPos.y() > r.top() && viewPos.y() < r.bottom()) return GripR;
        return r.contains(viewPos) ? GripMove : GripNone;
    }
    // 悬停光标:窗口内=移动掌,边/角=缩放箭头,未框选/窗外=十字
    void updateCursor(const QPoint& viewPos) {
        Qt::CursorShape c = Qt::CrossCursor;
        if (m_established) {
            switch (gripAt(viewPos)) {
            case GripMove: c = Qt::SizeAllCursor; break;
            case GripT: case GripB: c = Qt::SizeVerCursor; break;
            case GripL: case GripR: c = Qt::SizeHorCursor; break;
            case GripTL: case GripBR: c = Qt::SizeFDiagCursor; break;
            case GripTR: case GripBL: c = Qt::SizeBDiagCursor; break;
            default: c = Qt::CrossCursor; break;
            }
        }
        m_view->setCursor(c);
    }

    // 吸附到 MCU 整数倍:起点向原点方向对齐,尺寸向 MCU 对齐(至少 1 个 MCU)
    void snapSel() {
        const int iw = m_img.width(), ih = m_img.height();
        int x = std::max(0, (m_sel.x() / MCU) * MCU);
        int y = std::max(0, (m_sel.y() / MCU) * MCU);
        int w = std::max(MCU, (m_sel.width()  / MCU) * MCU);
        int h = std::max(MCU, (m_sel.height() / MCU) * MCU);
        if (x + w > iw) w = std::max(MCU, ((iw - x) / MCU) * MCU);
        if (y + h > ih) h = std::max(MCU, ((ih - y) / MCU) * MCU);
        if (x + w > iw) x = std::max(0, iw - w);
        if (y + h > ih) y = std::max(0, ih - h);
        m_sel = QRect(x, y, std::min(w, iw), std::min(h, ih));
    }

    QPoint toImage(const QPoint& viewPos) const {
        if (m_scale <= 0) return {};
        const int ox = (m_view->width()  - int(m_img.width()  * m_scale)) / 2;
        const int oy = (m_view->height() - int(m_img.height() * m_scale)) / 2;
        return QPoint(qBound(0, int((viewPos.x() - ox) / m_scale), m_img.width() - 1),
                      qBound(0, int((viewPos.y() - oy) / m_scale), m_img.height() - 1));
    }

    void updateView() {
        if (m_img.isNull()) { m_info->setText(gazeTr("无法读取该图片")); return; }
        const QSize avail = m_view->size() - QSize(16, 16);
        m_scale = std::min(double(avail.width())  / m_img.width(),
                           double(avail.height()) / m_img.height());
        if (m_scale > 1.0) m_scale = 1.0;

        QImage shown = m_img.scaled(int(m_img.width()  * m_scale),
                                    int(m_img.height() * m_scale),
                                    Qt::KeepAspectRatio, Qt::SmoothTransformation);
        // 选区外压暗,选区内原样 → 一眼看出裁到哪
        QPixmap canvas(shown.size());
        canvas.fill(QColor(10, 10, 14));
        QPainter p(&canvas);
        p.drawImage(0, 0, shown);
        const QRect r = QRect(int(m_sel.x() * m_scale), int(m_sel.y() * m_scale),
                              int(m_sel.width() * m_scale), int(m_sel.height() * m_scale));
        p.fillRect(canvas.rect(), QColor(0, 0, 0, 130));
        p.drawImage(r, shown, r);
        p.setPen(QPen(QColor("#4C9AF5"), 1));
        p.drawRect(r.adjusted(0, 0, -1, -1));
        p.end();
        m_view->setPixmap(canvas);

        const bool jpeg = isJpeg();
        m_info->setText(gazeTr(
            "裁剪区域 %1×%2  @(%3,%4)   原图 %5×%6   %7")
            .arg(m_sel.width()).arg(m_sel.height())
            .arg(m_sel.x()).arg(m_sel.y())
            .arg(m_img.width()).arg(m_img.height())
            .arg(jpeg ? gazeTr("已吸附到 16px 无损边界(JPEG 走 jpegtran -perfect)")
                      : gazeTr("非 JPEG:按像素精确裁剪(PNG 无损,其它格式会重编码)")));
        m_btnOk->setEnabled(m_sel.isValid() && m_sel.width() >= MCU && m_sel.height() >= MCU);
    }

    bool isJpeg() const {
        const QString e = QFileInfo(m_path).suffix().toLower();
        return e == QLatin1String("jpg") || e == QLatin1String("jpeg");
    }

    QString     m_path;
    QImage      m_img;
    QLabel*     m_view  = nullptr;
    QLabel*     m_info  = nullptr;
    QPushButton* m_btnOk   = nullptr;
    QPushButton* m_btnFull = nullptr;
    QRect       m_sel;
    QPoint      m_anchor;
    bool        m_dragging = false;
    bool        m_established = false;   // 第一次框选完成 = 裁剪窗口定形
    Grip        m_grip = GripNone;
    QPoint      m_pressView;             // 按下点(视图坐标;移动窗口用)
    QRect       m_origSel;               // 按下时的选区(移动/缩放基准)
    double      m_scale = 1.0;
};
