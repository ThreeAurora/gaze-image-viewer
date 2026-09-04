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

// 沿 parent 链找到声明了该槽的窗口(通常是 MainWindow)并调用:
// 预览面板不 include mainwindow.h,靠元对象签名解耦
//
// 两种写法互斥,实测(cache/tmp/invoke_sig.cpp):
//   indexOfMethod 只认规范签名 "name(types)"，裸名返回 -1
//   invokeMethod  只认裸方法名，它自己按 Q_ARG 拼签名
// 所以同一个字符串喂给两边必然一边失败 —— 探测用签名，调用前剥掉括号。
static QByteArray bareName(const char* slot) {
    QByteArray s(slot);
    const int p = s.indexOf('(');
    if (p >= 0) s.truncate(p);
    return s;
}

static void invokeOnWindow(QObject* from, const char* slot) {
    const QByteArray name = bareName(slot);
    for (QObject* w = from; w; w = w->parent()) {
        if (w->metaObject()->indexOfMethod(slot) >= 0) {
            QMetaObject::invokeMethod(w, name.constData());
            return;
        }
    }
}

static void invokeOnWindow(QObject* from, const char* slot, const QString& arg) {
    const QByteArray name = bareName(slot);
    for (QObject* w = from; w; w = w->parent()) {
        if (w->metaObject()->indexOfMethod(slot) >= 0) {
            QMetaObject::invokeMethod(w, name.constData(), Q_ARG(QString, arg));
            return;
        }
    }
}

void PreviewPanel::resizeEvent(QResizeEvent* event) {
    QWidget::resizeEvent(event);
    if (m_mode == "image") fitAuto();
    applyViewerChrome();     // 滚动条/信息条/浮动条/导航小窗都贴边,尺寸变了要重定位
    syncVideoChildren();
    if (m_liveBadge && m_liveBadge->isVisible())
        m_liveBadge->move(m_videoWidget ? m_videoWidget->width() - m_liveBadge->width() - 12 : 0, 12);
    updateRawFullBtn();      // #140b:悬浮 RAW 钮贴右上角,面板缩放跟着挪
}

void PreviewPanel::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) m_lastPressTs = event->timestamp();
    if (event->button() == Qt::LeftButton && m_mode == "image" && m_origPix) {
        // 动态照片:单击=重播动态部分(临时 1:1 放大只属于普通图片)
        if (m_liveInfo) { playLivePhoto(); event->accept(); return; }
        // 左键动作按修饰符查配置(设置→鼠标;0=缩放与移动 1=什么都不做)
        const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
        const char* key = "Mouse/leftNone";
        if (mods & Qt::ControlModifier)      key = "Mouse/leftCtrl";
        else if (mods & Qt::AltModifier)     key = "Mouse/leftAlt";
        else if (mods & Qt::ShiftModifier)   key = "Mouse/leftShift";
        const bool allowDrag =
            AppSettings::instance().get(key,
                key == QString("Mouse/leftNone") || key == QString("Mouse/leftCtrl") ? 0 : 1).toInt() == 0;
        if (m_isGif) {
            // #97:GIF 用视频那套交互 —— 单击 = 播放/暂停,按下不做临时 1:1 放大。
            // 拖动平移仍然可用来放大后的 GIF 上,故不吞掉 allowDrag。
            m_gifPressPos = event->pos();
            m_gifToggleArm = true;
            if (allowDrag) {
                m_dragging = true;
                m_dragStart = event->pos();
                m_dragLabelPos = m_imgLabel->pos();
                setCursor(Qt::ClosedHandCursor);
            }
            event->accept();
            return;
        }
        if (!allowDrag) {
            event->accept();
            return;
        }
        m_dragging = true;
        m_dragStart = event->pos();
        m_dragLabelPos = m_imgLabel->pos();
        if (!m_ctrlZoomed) {
            // 左键临时 1:1:光标下那个点原地不动(#101),松开还原
            m_tempZoom = true;
            zoomAnchored(oneToOneScale(), m_dragStart.toPoint());
            setCursor(Qt::ClosedHandCursor);
        } else {
            setCursor(Qt::ClosedHandCursor);
        }
    }
    QWidget::mousePressEvent(event);
}

void PreviewPanel::mouseMoveEvent(QMouseEvent* event) {
    if (m_cursorHidden) restoreCursor();
    else m_cursorTimer.start();
    // 按下去后又拖出一段距离 = 平移,不是单击,#97 的播放/暂停因此不该触发
    if (m_gifToggleArm && (event->pos() - m_gifPressPos).manhattanLength() > 4)
        m_gifToggleArm = false;
    if (m_dragging && m_mode == "image") {
        QPointF delta = event->pos() - m_dragStart;
        m_imgLabel->move(clampedLabelPos((m_dragLabelPos + delta).toPoint()));
        updateOverlayScrollbars();
        updatePanTool();
    }
    const QPoint cur = event->position().toPoint();
    updateFloatBar(&cur);            // Fullscreen/floatView:靠近顶/右边缘浮现
    updateInfoBar(&cur);             // 2026-09-02:全屏信息仅在光标到顶时浮现
    QWidget::mouseMoveEvent(event);
}

// 指针从"全屏闲置隐藏"中恢复
void PreviewPanel::restoreCursor() {
    m_cursorHidden = false;
    unsetCursor();
    m_imgLabel->unsetCursor();
    m_cursorTimer.start();
}

void PreviewPanel::mouseReleaseEvent(QMouseEvent* event) {
    if (event->button() == Qt::LeftButton) {
        if (m_gifToggleArm) {
            m_gifToggleArm = false;
            setGifPaused(!m_gifPaused);   // #97:单击 GIF 画面 = 播放/暂停
        }
        m_dragging = false;
        unsetCursor();
        if (m_tempZoom) {
            m_tempZoom = false;
            m_imgLabel->move(0, 0);
            fitAuto();          // 松开还原适应窗口
        }
    }
    QWidget::mouseReleaseEvent(event);
}

void PreviewPanel::mouseDoubleClickEvent(QMouseEvent* event) {
    // #159:双击判定自管上限。Qt 默认跟系统(400ms),用户嫌松 —— 两次按下间隔
    // 超过 300ms 的"双击"不切模式,落成两次单击各自的本职(GIF 暂停/临时 1:1)。
    // 无符号减法:timestamp 回绕也正确
    if (event->button() == Qt::LeftButton
        && event->timestamp() - m_lastPressTs <= 300) {
        // 2026-09-02 用户令:双击预览区 = 开一个查看器标签页。#221 起按态分流
        // 在 MainWindow::previewDoubleClicked 里做(浏览器=开签,查看器=关签回
        // 浏览器,G 全屏=先退全屏),面板自己不看模式;Ctrl 按住仍是后台开签。
        const bool ctrl = (event->modifiers() & Qt::ControlModifier) != 0;
        invokeOnWindow(this, ctrl ? "openTabBackground()" : "previewDoubleClicked()");
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void PreviewPanel::wheelEvent(QWheelEvent* event) {
    // 滚轮动作按修饰符查配置(0=上一个/下一个 1=放大/缩小 2=什么都不做)
    Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    if (QApplication::mouseButtons() & Qt::RightButton) {
        mods |= Qt::ControlModifier;   // 按住右键+滚轮 = Ctrl+滚轮缩放(2026-08-30 裁决)
        m_rbtnWheel = true;
    }
    const char* key = "Mouse/wheelNone";
    if (mods & Qt::ControlModifier)      key = "Mouse/wheelCtrl";
    else if (mods & Qt::AltModifier)     key = "Mouse/wheelAlt";
    else if (mods & Qt::ShiftModifier)   key = "Mouse/wheelShift";
    const int action = AppSettings::instance().get(key, key == QString("Mouse/wheelCtrl") ? 1 : 0).toInt();

    if (action == 1) {
        if (m_mode == "image") {
            const int delta = event->angleDelta().y();
            const double target = pp_impl::s_int("Viewer/zoomMode", 1) == 0
                ? stepZoom(m_scale, delta > 0)                    // 固定档位
                : qBound(0.01, m_scale * (delta > 0 ? 1.25 : 0.8), 10.0);
            m_ctrlZoomed = true;   // 缩放后:左键变纯拖动
            // 以光标为锚(#100)。坐标从全局换算:滚轮可能先落到 m_imgLabel /
            // 导航小窗 / 覆盖滚动条这些子控件上,冒泡上来的 pos 不一定是本面板坐标
            zoomAnchored(target, mapFromGlobal(event->globalPosition().toPoint()));
        }
    } else if (action == 0) {
        int delta = event->angleDelta().y();
        emit navFile(delta > 0 ? -1 : 1);
    }
    // action == 2:什么都不做
}

// ── Mouse/right{None,Ctrl,Alt,Shift}:0=上下文菜单 1=什么都不做 ──
void PreviewPanel::contextMenuEvent(QContextMenuEvent* event) {
    if (m_rbtnWheel) {   // 右键+滚轮缩放松开右键的那一下:不弹菜单
        m_rbtnWheel = false;
        event->accept();
        return;
    }
    const Qt::KeyboardModifiers mods = QApplication::keyboardModifiers();
    const char* suffix = "None";
    if (mods & Qt::ControlModifier)      suffix = "Ctrl";
    else if (mods & Qt::AltModifier)     suffix = "Alt";
    else if (mods & Qt::ShiftModifier)   suffix = "Shift";
    if (pp_impl::s_int(QString::fromLatin1("Mouse/right") + suffix, 0) != 0) {
        event->accept();
        return;
    }
    if (m_filePath.isEmpty()) { event->ignore(); return; }

    const bool isImage = m_mode == "image";
    const bool hasMedia = m_mode == "video" || m_mode == "audio";
    QMenu menu(this);
    menu.addAction(gazeTr("上一个文件"), this, [this]() { emit navFile(-1); });
    menu.addAction(gazeTr("下一个文件"), this, [this]() { emit navFile(1); });
    if (isImage) {
        menu.addSeparator();
        menu.addAction(gazeTr("适应窗口"), this, [this]() { fitAuto(); });
        menu.addAction(gazeTr("1:1 像素"), this, [this]() {
            if (!m_origPix) return;
            m_scale = oneToOneScale(); m_ctrlZoomed = true; render();
        });
    }
    if (hasMedia || m_isLivePhoto) {
        menu.addAction(gazeTr("播放/暂停"), this, [this]() { togglePlayPause(); });
    }
    // #240(2026-09-04 用户令):文本预览的两个形态开关进右键 —— 自动换行
    // (txt/md 纯文本)与 Markdown 渲染样式(md 专属),勾选态即时生效
    if (m_mode == "text") {
        menu.addSeparator();
        QAction* wrap = menu.addAction(gazeTr("自动换行"));
        wrap->setCheckable(true);
        wrap->setChecked(pp_impl::s_bool("Preview/textWrap", true));
        connect(wrap, &QAction::toggled, this, [this](bool on) {
            AppSettings::instance().set("Preview/textWrap", on);
            m_textEdit->setWordWrapMode(on ? QTextOption::WordWrap
                                           : QTextOption::NoWrap);
        });
        if (m_filePath.endsWith(QStringLiteral(".md"), Qt::CaseInsensitive)) {
            QAction* md = menu.addAction(gazeTr("以 Markdown 样式展示"));
            md->setCheckable(true);
            md->setChecked(pp_impl::s_bool("Preview/mdRenderStyle", true));
            connect(md, &QAction::toggled, this, [this](bool on) {
                AppSettings::instance().set("Preview/mdRenderStyle", on);
                loadFile(m_filePath);   // 重新分发:渲染样式 ↔ 纯文本
            });
        }
    }
    // #224(2026-09-04 用户令):G 全屏预览进右键菜单 —— 图片/视频/音频统一
    // 都有本地入口(进查看器后网格那份右键够不着,G 键也不总在手边)
    menu.addAction(gazeTr("全屏预览  (G)"), this, [this]() {
        invokeOnWindow(this, "toggleFullView()");
    });
    menu.addSeparator();
    menu.addAction(gazeTr("用系统默认程序打开"), this, [this]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_filePath));
    });
    // 进查看器后网格是隐藏的,浏览器那份右键菜单够不着:另起标签要有本地入口
    menu.addAction(gazeTr("在新标签卡中打开"), this, [this]() {
        invokeOnWindow(this, "openViewerTab(QString)", m_filePath);
    });
    menu.addAction(gazeTr("复制文件"), this, [this]() {
        auto* mime = new QMimeData;
        mime->setUrls({ QUrl::fromLocalFile(m_filePath) });
        QApplication::clipboard()->setMimeData(mime);
    });
    menu.addAction(gazeTr("复制文件路径"), this, [this]() {
        QApplication::clipboard()->setText(m_filePath);
    });
    menu.addSeparator();
    menu.addAction(gazeTr("删除"), this, [this]() {
        const QString gone = m_filePath;
        if (!deleteWithSettings({ gone }, this)) return;
        clear();
        invokeOnWindow(this, "reloadAfterDelete(QString)", gone);
    });
    menu.exec(event->globalPos());
}
// ═══════════════════════════════════════════
// 查看器快捷键(ini ViewerShortcut/*;动作表与设置页共用 viewerhotkeys.h)
// ═══════════════════════════════════════════

void PreviewPanel::ensureHotkeys() {
    if (m_hotkeysLoaded) return;
    m_hotkeysLoaded = true;
    reloadViewerHotkeys();
}

void PreviewPanel::reloadViewerHotkeys() {
    m_viewerHotkeys.clear();
    for (const auto& d : viewerHotkeyCmds()) {
        const QString name = QString::fromUtf8(d.name);
        const QString v = AppSettings::instance().get(
            QStringLiteral("ViewerShortcut/") + name, QString::fromLatin1(d.defKey)).toString();
        QKeySequence ks(v);
        if (!ks.isEmpty())
            m_viewerHotkeys.insert(name, ks);
    }
}

QString PreviewPanel::hotkeyAction(QKeyEvent* e) const {
    const QKeySequence pressed(int(e->key()) | int(e->modifiers()));
    if (pressed.isEmpty()) return {};
    for (auto it = m_viewerHotkeys.constBegin(); it != m_viewerHotkeys.constEnd(); ++it)
        if (it.value() == pressed) return it.key();
    return {};
}

// 主窗口过滤器用它决定"这个键归查看器表"，不再按浏览器键位抢
bool PreviewPanel::claimsHotkey(QKeyEvent* e) {
    ensureHotkeys();
    return !hotkeyAction(e).isEmpty();
}

// 浏览器态媒体键(2026-09-03):预览面板正在显示媒体(视频/音频/GIF)时,
// 查看器表的"播放/暂停""停止"在浏览器里同样生效 —— 浏览器选中视频、预览
// 自动播放后按 T 停止是基本诉求(此前 T 只在查看器模式作数,浏览器态落空)。
// 其余动作(缩放/适应窗口/切换文件)是查看器语义,浏览器不接;与浏览器
// 字母键(F/D/G)的撞车只在用户自定义改键时发生,由调用方顺序决定优先级。
bool PreviewPanel::handleBrowserMediaKey(QKeyEvent* e) {
    if (m_mode != "video" && m_mode != "audio" && !m_isGif) return false;
    ensureHotkeys();
    const QString act = hotkeyAction(e);
    if (act == gazeTr("播放/暂停")) {  // 播放/暂停
        togglePlayPause();
        return true;
    }
    if (act == gazeTr("停止")) {  // 停止:与 keyPressEvent 同逻辑
        if (m_isGif) {
            setGifPaused(true);   // 先停:跳帧走暂停态,避开运行态 jumpToFrame 卡死
            gifSeekMs(0);
        } else if (m_player) {
            m_player->stop();     // Qt6 stop 同时把位置归零 → 再播从头开始
            m_progress->setValue(0);
        }
        return true;
    }
    return false;
}

void PreviewPanel::keyPressEvent(QKeyEvent* event) {
    // 这张表只在查看器里作数:实测焦点不会随策略降级/窗格复显自动交还
    // (cache/tmp/focus_probe.cpp)，万一它还留在这块面板上，不闸一下浏览器
    // 的方向键就被这里吞掉。浏览器侧的 ESC 自有主窗口应用级过滤器管。
    if (!m_viewerMode) { QWidget::keyPressEvent(event); return; }
    ensureHotkeys();
    const QString act = hotkeyAction(event);
    if (event->key() == Qt::Key_Escape) {
        // ESC 固定:查看器退回浏览器 / 退出全屏
        QObject* mw = this;
        while (mw && mw->metaObject()->indexOfMethod("viewerBack()") < 0)
            mw = mw->parent();
        if (mw) QMetaObject::invokeMethod(mw, "viewerBack");
        return;
    }
    if (act == gazeTr("下一个文件")) {   // 下一个文件
        emit navFile(1);  event->accept();  return;
    }
    if (act == gazeTr("上一个文件")) {   // 上一个文件
        emit navFile(-1); event->accept();  return;
    }
    if (m_mode == "image" &&
        (act == gazeTr("放大") ||          // 放大
         act == gazeTr("缩小"))) {         // 缩小
        const bool up = (act == gazeTr("放大"));
        // Viewer/zoomMode:0=固定档位跳跃,1=连续 1.25/0.8 无级缩放(默认)
        m_scale = pp_impl::s_int("Viewer/zoomMode", 1) == 0
                ? stepZoom(m_scale, up)
                : qBound(0.01, m_scale * (up ? 1.25 : 0.8), 10.0);
        m_ctrlZoomed = true;
        render();  event->accept();  return;
    }
    if (m_mode == "image" && act == gazeTr("适应窗口")) {  // 适应窗口
        fitAuto();  event->accept();  return;
    }
    if (m_mode == "image" && m_origPix &&
        act == gazeTr("1:1 像素")) {       // 1:1 像素
        m_scale = oneToOneScale();  m_ctrlZoomed = true;  render();  event->accept();  return;
    }
    if (act == gazeTr("播放/暂停")) {  // 播放/暂停
        togglePlayPause();  event->accept();  return;
    }
    if (act == gazeTr("停止")) {  // 停止(回到开头,默认 T;与 m_btnStop 同逻辑)
        if (m_isGif) {
            setGifPaused(true);   // 先停:跳帧走暂停态,避开运行态 jumpToFrame 卡死
            gifSeekMs(0);
        } else if (m_player) {
            m_player->stop();     // Qt6 stop 同时把位置归零 → 再播从头开始
            m_progress->setValue(0);
        }
        event->accept();  return;
    }
    QWidget::keyPressEvent(event);
}

bool PreviewPanel::eventFilter(QObject* obj, QEvent* event) {
    // 波形画布尺寸变化 → 重画(不消费事件;首次布局/查看器切换都靠它跟上)
    if (obj == m_waveLabel && event->type() == QEvent::Resize) renderWave();
    // #115:文本预览的滚轮 = 上一个/下一个文件,永不滚文本。QTextEdit 的视口
    // 会自己吃掉 Wheel 并接受,冒泡不到面板 wheelEvent,只能拦在过滤器里。
    // 与图片区滚轮的 Mouse/wheel* 修饰键配置不同流:这里是硬规定(需求原话
    // 「文本滚轮=切换文件」),文本没有缩放语义,Ctrl/Shift 落在这也一并切换。
    if (obj == m_textEdit->viewport() && event->type() == QEvent::Wheel) {
        const int delta = static_cast<QWheelEvent*>(event)->angleDelta().y();
        emit navFile(delta > 0 ? -1 : 1);
        return true;
    }
    // 视频区(它盖住面板,鼠标事件到不了 mousePressEvent):
    //   左键 = 播放/暂停;双击 = 浏览器↔查看器(与图片区一致)
    if ((obj == m_videoWidget || obj == m_vw) && m_mode == "video"
        && (event->type() == QEvent::MouseButtonPress
            || event->type() == QEvent::MouseButtonDblClick)) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (me->button() == Qt::LeftButton) {
            if (event->type() == QEvent::MouseButtonDblClick) {
                invokeOnWindow(this, "toggleViewer()");
            } else {
                togglePlayPause();
            }
            return true;
        }
    }
    // 时间标签点击:已播/总时长 ↔ 剩余/总时长
    if (obj == m_timeLabel && event->type() == QEvent::MouseButtonRelease) {
        m_timeRemaining = !m_timeRemaining;
        if (m_isGif) {
            gifSyncToFrame(m_gifFrameIdx);
            return true;
        }
        if (m_player) {
            qint64 pos = m_player->position();
            qint64 dur = m_player->duration();
            auto fmt = [](qint64 ms) -> QString {
                int sec = static_cast<int>(ms / 1000);
                if (sec < 0) sec = 0;   // seek 期间 dur < pos → 剩余为负
                int h = sec / 3600;
            return h > 0
                ? QString("%1:%2:%3").arg(h).arg((sec % 3600) / 60, 2, 10, QChar('0')).arg(sec % 60, 2, 10, QChar('0'))
                : QString("%1:%2").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
            };
            qint64 shown = m_timeRemaining ? qMax(qint64(0), dur - pos) : pos;
            m_timeLabel->setText(fmt(shown) + " / " + fmt(dur));
        }
        return true;
    }
    // 播放条:点击=跳转,按住拖动=擦洗。旧写法把左键按下整段吃掉,
    // QSlider 永远进不了拖动态 → 只能点、不能拖(#97 现场反馈)。
    // 这里自己接管 press/move/release 三步;setSliderDown 兼当拖动标志。
    if (obj == m_progress) {
        if (event->type() == QEvent::MouseButtonPress) {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton) return false;
            m_progress->setSliderDown(true);
            progressScrub(me->position().x());
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_progress->isSliderDown()) {
            progressScrub(static_cast<QMouseEvent*>(event)->position().x());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && m_progress->isSliderDown()) {
            m_progress->setSliderDown(false);
            return true;
        }
        if (event->type() == QEvent::MouseButtonDblClick) return true;
    }
    // ── 导航小窗拖动:按下即抓取(grabMouse 保证拖出小窗也持续跟随), ──
    //    移动中视口中心跟随指尖;松开结束。点哪蓝框就到哪
    if (obj == m_panThumb || obj == m_panView) {
        auto* me = static_cast<QMouseEvent*>(event);
        if (event->type() == QEvent::MouseButtonPress
            && me->button() == Qt::LeftButton && m_mode == "image") {
            m_navDragging = true;
            m_panThumb->grabMouse();
            // 统一经共同父(m_panTool)换算,兼容按在蓝框或缩略图上
            const QPoint inTool = static_cast<QWidget*>(obj)->mapTo(m_panTool,
                                                                    me->position().toPoint());
            panNavTo(m_panThumb->mapFrom(m_panTool, inTool));
            return true;
        }
        if (event->type() == QEvent::MouseMove && m_navDragging) {
            // 抓取方是 m_panThumb:拖动事件的 pos 都是它的坐标
            panNavTo(me->position().toPoint());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease && m_navDragging) {
            m_navDragging = false;
            m_panThumb->releaseMouse();
            return true;
        }
    }
    return QWidget::eventFilter(obj, event);
}

void PreviewPanel::togglePlayPause() {
    // #97:GIF 走同一入口,m_player 此刻挂着的可能是上一条视频,必须先分流再判它
    if (m_isGif) {
        setGifPaused(!m_gifPaused);
        return;
    }
    // 只在音/视频预览时生效:切到图片等模式后 m_player 对象仍挂着上一条
    // 媒体,无守卫时空格会把之前的视频/音频重新播出来
    if (!m_player || (m_mode != "video" && m_mode != "audio")) return;
    if (m_player->playbackState() == QMediaPlayer::PlayingState)
        m_player->pause();
    else
        m_player->play();
}

void PreviewPanel::seekDelta(int seconds) {
    if (m_isGif) {
        gifSeekMs(m_progress->value() + seconds * 1000);
        return;
    }
    if (!m_player) return;
    qint64 pos = m_player->position() + seconds * 1000;
    pos = qBound(0LL, pos, m_player->duration());
    m_player->setPosition(pos);
}
