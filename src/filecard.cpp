#include "filecard.h"
#include "contextmenu.h"
#include "livephoto.h"
#include "labelstore.h"
#include "constants.h"
#include "perflog.h"

#include <QPainter>
#include <QPainterPath>
#include <QPen>
#include <QMouseEvent>
#include <QContextMenuEvent>
#include <QApplication>

FileCard::FileCard(QWidget* parent) : QFrame(parent) {
    setMouseTracking(true);
    // 光标保持默认箭头(不用小手);卡片本体透明,图片之外纯黑
    setStyleSheet("FileCard{background:transparent;border:none;}");

    m_thumbLabel = new QLabel(this);
    m_thumbLabel->setAlignment(Qt::AlignCenter);
    m_thumbLabel->setStyleSheet("background:transparent;");

    m_nameLabel = new QLabel(this);
    m_nameLabel->setAlignment(Qt::AlignCenter);
    m_nameLabel->setAutoFillBackground(true);

    // LIVE 徽章:半透明黑胶囊 + 红点
    m_liveBadge = new QLabel("LIVE", this);
    m_liveBadge->setStyleSheet(
        "background:rgba(0,0,0,150);color:#FFF;font-size:9px;font-weight:bold;"
        "padding:2px 7px;border-radius:8px;border:1px solid rgba(255,255,255,60);");
    m_liveBadge->hide();

    // 标记星
    m_starLabel = new QLabel(QString::fromUtf8("\xe2\x98\x85"), this); // ★
    m_starLabel->setStyleSheet(
        "color:#E8B339;font-size:14px;background:transparent;");
    m_starLabel->hide();
}

void FileCard::setup(const FileEntry& entry, int size, int viewMode, int height) {
    m_cardSize   = size;
    m_filePath   = entry.path;
    m_active     = true;
    m_selected   = false;
    m_isLive     = false;
    m_selColor   = C_SELECT_BLUE;
    m_liveBadge->hide();

    // 文件名底色:格式标签颜色(设置页"缩略图→标签颜色"可配;总开关关闭时透明)
    m_nameBg = LabelColors::enabled()
        ? LabelColors::colorForExt(entry.ext.mid(1)).name(QColor::HexRgb)
        : QStringLiteral("transparent");

    // hover tooltip:完整文件名 + 创建时间 + 修改时间 + 大小
    {
        QFileInfo fi(entry.path);
        QDateTime birth = fi.birthTime();
        QDateTime mod   = fi.lastModified();
        QString tip = entry.name + "\n"
            + "\xe5\x88\x9b\xe5\xbb\xba: " + (birth.isValid()
                ? birth.toString("yyyy/MM/dd - HH:mm:ss")
                : QString::fromUtf8("\xe2\x80\x94")) + "\n"
            + "\xe4\xbf\xae\xe6\x94\xb9: " + (mod.isValid()
                ? mod.toString("yyyy/MM/dd - HH:mm:ss")
                : QString::fromUtf8("\xe2\x80\x94")) + "\n"
            + formatSize(entry.size);
        setToolTip(tip);
    }

    // ── 按查看方式布局 ──
    int ts;
    switch (viewMode) {
    case VM_THUMBS: {                       // 纯缩略图,无名称
        ts = size - 4;
        setFixedSize(size, size + 4);
        m_thumbLabel->setGeometry(2, 2, ts, ts);
        m_thumbRect = QRect(2, 2, ts, ts);
        m_nameLabel->hide();
        if (m_detailLabel) m_detailLabel->hide();
        break;
    }
    case VM_THUMBS_LABEL:                   // 缩略图 + 标签(名称+色圈加大)
    case VM_THUMBS_NAME: {                  // 缩略图 + 文件名(默认)
        ts = size - 8;
        setFixedSize(size, size + 22);
        m_thumbLabel->setGeometry(4, 4, ts, ts);
        m_nameLabel->setGeometry(2, size + 2, size - 4, 18);
        m_nameLabel->show();
        m_thumbRect = QRect(4, 4, ts, ts);
        if (m_detailLabel) m_detailLabel->hide();
        break;
    }
    case VM_THUMBS_DETAIL: {                // 缩略图 + 详细(名称 + 大小/日期)
        ts = size - 8;
        setFixedSize(size, size + 42);
        m_thumbLabel->setGeometry(4, 4, ts, ts);
        m_nameLabel->setGeometry(2, size + 2, size - 4, 18);
        m_nameLabel->show();
        if (!m_detailLabel) {
            m_detailLabel = new QLabel(this);
            m_detailLabel->setStyleSheet("color:#FFFFFF;font-size:10px;background:transparent;");
        }
        m_detailLabel->setGeometry(2, size + 20, size - 4, 16);
        m_detailLabel->setAlignment(Qt::AlignCenter);
        m_detailLabel->setText(formatSize(entry.size) + "  "
            + QDateTime::fromSecsSinceEpoch(static_cast<qint64>(entry.mtime))
                  .toString("yyyy/M/d HH:mm"));
        m_detailLabel->show();
        m_thumbRect = QRect(4, 4, ts, ts);
        break;
    }
    case VM_ICONS: {                        // 图标(小图 + 名称)
        ts = 80;
        setFixedSize(88, 106);
        m_thumbLabel->setGeometry(4, 4, ts, ts);
        m_nameLabel->setGeometry(2, 86, 84, 18);
        m_nameLabel->show();
        m_thumbRect = QRect(4, 4, ts, ts);
        if (m_detailLabel) m_detailLabel->hide();
        break;
    }
    case VM_LIST: {                         // 列表(单行)
        int w = height > 0 ? height : 600;    // height 复用为行宽
        setFixedSize(w, 24);
        m_thumbLabel->setGeometry(3, 3, 18, 18);
        m_nameLabel->setGeometry(26, 3, w - 30, 18);
        m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_nameLabel->show();
        m_thumbRect = QRect(3, 3, 18, 18);
        if (m_detailLabel) m_detailLabel->hide();
        break;
    }
    case VM_DETAILS: {                      // 详细信息(单行多列)
        int w = height > 0 ? height : 600;
        setFixedSize(w, 26);
        m_thumbLabel->setGeometry(3, 3, 20, 20);
        m_nameLabel->setGeometry(28, 4, static_cast<int>(w * 0.38), 18);
        m_nameLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_nameLabel->show();
        if (!m_detailLabel) {
            m_detailLabel = new QLabel(this);
            m_detailLabel->setStyleSheet("color:#FFFFFF;font-size:11px;background:transparent;");
        }
        m_detailLabel->setGeometry(static_cast<int>(w * 0.40), 5, static_cast<int>(w * 0.58), 16);
        m_detailLabel->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        m_detailLabel->setText(QString("%1    %2    %3")
            .arg(formatSize(entry.size), -12)
            .arg(mimeType(entry.ext), -14)
            .arg(QDateTime::fromSecsSinceEpoch(static_cast<qint64>(entry.mtime))
                     .toString("yyyy/M/d HH:mm")));
        m_detailLabel->show();
        m_thumbRect = QRect(3, 3, 20, 20);
        break;
    }
    default: {                                // VM_WATERFALL:纯图,高度由外部按宽高比定
        ts = size - 4;
        setFixedSize(size, height > 0 ? height : size);
        m_thumbLabel->setGeometry(2, 2, ts, height > 0 ? height - 4 : ts);
        m_thumbRect = QRect(2, 2, ts, height > 0 ? height - 4 : ts);
        m_nameLabel->hide();
        if (m_detailLabel) m_detailLabel->hide();
        m_cover = true;                       // 缩略图 cover 填满(比例≈原图,几乎不裁)
        break;
    }
    }
    if (viewMode != VM_WATERFALL) m_cover = false;

    // 名称文本
    QFontMetrics fm = m_nameLabel->fontMetrics();
    QString elided = fm.elidedText(entry.name, Qt::ElideMiddle,
                                   qMax(40, m_nameLabel->width() - 6));
    m_nameLabel->setText(elided);

    // 图标占位(无缩略图时):文件夹与文件类型图标都顶满缩略图高度
    QIcon icon = entry.isDir ? folderIcon(ts) : typeIcon(entry.ext, entry.path);
    int isz = m_thumbLabel->width();
    QPixmap pm = icon.pixmap(isz, isz);
    if (pm.width() != isz || pm.height() != isz)
        pm = pm.scaled(isz, isz, Qt::KeepAspectRatio, Qt::SmoothTransformation);  // 平滑缩放顶满
    m_thumbLabel->setPixmap(pm);

    m_liveBadge->adjustSize();
    m_liveBadge->move(8, 8);
    m_starLabel->setGeometry(width() - 24, 4, 20, 20);
    m_marked = false;
    m_starLabel->hide();

    setColorLabel(entry.colorLabel);
    applyLabelBg();
    update();
}

void FileCard::setColorLabel(int color) {
    if (m_colorLabel == color) return;
    m_colorLabel = color;
    update();
}

void FileCard::setThumbnail(const QPixmap& pixmap) {
    if (!m_active || pixmap.isNull()) return;
    int tw = m_thumbLabel->width();
    int th = m_thumbLabel->height();
    if (tw <= 0 || th <= 0) return;
    // 瀑布流(cover):按比例填满卡片(卡片比例≈原图比例,几乎不裁);
    // 其余模式(fit):完整显示,居中留白
    Qt::AspectRatioMode am = m_cover
        ? Qt::KeepAspectRatioByExpanding : Qt::KeepAspectRatio;
    QPixmap scaled = pixmap.scaled(tw, th, am, Qt::SmoothTransformation);
    QPixmap square(tw, th);
    square.fill(Qt::transparent);
    QPainter p(&square);
    p.setRenderHint(QPainter::Antialiasing);
    p.drawPixmap((tw - scaled.width()) / 2, (th - scaled.height()) / 2, scaled);
    p.end();
    QPixmap rounded(tw, th);
    rounded.fill(Qt::transparent);
    QPainter rp(&rounded);
    rp.setRenderHint(QPainter::Antialiasing);
    QPainterPath path;
    path.addRoundedRect(0, 0, tw, th, 4, 4);
    rp.setClipPath(path);
    rp.drawPixmap(0, 0, square);
    rp.end();
    m_thumbLabel->setPixmap(rounded);

    // 选中框贴图片实际显示区域(随图片宽高变化)——用户确认的正确行为
    m_thumbRect = QRect(m_thumbLabel->x() + (tw - scaled.width()) / 2,
                        m_thumbLabel->y() + (th - scaled.height()) / 2,
                        scaled.width(), scaled.height());
    update();
}

void FileCard::setSelected(bool sel, bool multi) {
    if (m_selected == sel && m_selColor == (multi ? C_SELECT_YELLOW : C_SELECT_BLUE))
        return;
    m_selected = sel;
    m_selColor = multi ? C_SELECT_YELLOW : C_SELECT_BLUE;
    applyLabelBg();
    update();
}

void FileCard::setMarked(bool marked) {
    m_marked = marked;
    m_starLabel->setVisible(marked);
}

void FileCard::setMarked(bool marked) {
    m_marked = marked;
    m_starLabel->setVisible(marked);
}

void FileCard::deactivate() {
    m_active = false;
    hide();  // 立即隐藏，防止旧数据残留
}

void FileCard::detectLivePhoto() {
    if (!m_active) return;
    auto info = LivePhoto::detect(m_filePath);
    if (info.has_value() && m_active) {
        m_isLive = true;
        m_liveBadge->show();
    }
}

// ═══ 绘制 ═══
void FileCard::paintEvent(QPaintEvent* event) {
    PerfLog::Scope _perf("FileCard::paintEvent", 3);
    QFrame::paintEvent(event);
    if (!m_active) return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    // 选中:直角蓝框紧贴缩略图本身(随图片宽高变化)
    if (m_selected) {
        QRect r = m_thumbRect.adjusted(-2, -2, 2, 2);
        p.setPen(QPen(QColor(m_selColor), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(r);
    }

    // hover:缩略图白色描边(醒目但不笨重;有颜色标记时让位)
    if (m_hovered && !m_selected && m_colorLabel == 0) {
        p.setPen(QPen(QColor(255, 255, 255, 220), 2));
        p.setBrush(Qt::NoBrush);
        p.drawRect(m_thumbRect.adjusted(-2, -2, 1, 1));
    }

    // 颜色标记:缩略图左上角彩色圆圈(白描边)
    if (m_colorLabel > 0) {
        QColor c = LabelStore::colorValue(m_colorLabel);
        if (c.isValid()) {
            QPointF center = m_thumbRect.topLeft() + QPointF(9, 9);
            p.setPen(QPen(QColor("#FFFFFF"), 1.5));
            p.setBrush(c);
            p.drawEllipse(center, 7, 7);
        }
    }
}

void FileCard::applyLabelBg() {
    // 用内联 styleSheet(优先级高于全局 QSS;palette 会被 QWidget 全局样式压制)
    // 用内联 styleSheet(优先级高于全局 QSS;palette 会被 QWidget 全局样式压制)
    // 用内联 styleSheet(优先级高于全局 QSS;palette 会被 QWidget 全局样式压制)
    // 选中 = 蓝底纯白字;未选中 = 格式底色 + 正常白字 / 隐藏文件淡灰字
    QString bg = m_selected ? m_selColor : m_nameBg;
    QString color = m_selected ? QStringLiteral("#FFFFFF")
                               : (m_hidden ? QStringLiteral(C_TEXT_HIDDEN)
                                           : QStringLiteral("#FFFFFF"));
    QString style = QString("QLabel{background:%1;color:%2;}").arg(bg, color);
    // 值没变就跳过:setStyleSheet 每次都要重解析样式+重打磨控件,
    // setThumbnail 高频调用时这是实打实的开销
    if (style == m_appliedBg) return;
    m_appliedBg = style;
    m_nameLabel->setAutoFillBackground(false);
    m_nameLabel->setStyleSheet(style);
}

// ═══ 鼠标事件 ═══
void FileCard::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        emit clicked(this);
    QFrame::mouseReleaseEvent(event);
}

void FileCard::mouseDoubleClickEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton)
        emit doubleClicked(this);
    QFrame::mouseDoubleClickEvent(event);
}

void FileCard::enterEvent(QEnterEvent* event) {
    m_hovered = true;
    update();   // hover 态由 paintEvent 统一绘制
    QFrame::enterEvent(event);
}

void FileCard::leaveEvent(QEvent* event) {
    m_hovered = false;
    update();
    QFrame::leaveEvent(event);
}

void FileCard::contextMenuEvent(QContextMenuEvent* event) {
    FileContextMenu menu(this);
    menu.exec(event->globalPos());
}
