#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QAbstractButton>
#include <QPushButton>
#include <QLineEdit>
#include <QTextEdit>
#include <QPlainTextEdit>
#include <QAbstractSpinBox>
#include <QComboBox>
#include <QAbstractItemView>
#include <QMenu>
#include <QEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include "mainwindow.h"
#include "constants.h"
#include "keytarget.h"
#include "settings.h"
#include "thumbnailer.h"
#include "logger.h"

// 全局对话框键盘语义:Space → 确认(Yes/OK/确定),Yes/No 框的 Esc → No,
// Enter 交 QDialog 原生(默认按钮)。键事件送达的是焦点控件本身 —— 按收件
// 反查所在对话框再点它的"肯定"按钮;旧版只拦"QMessageBox 本身是收件"的
// 情况,焦点落在按钮以外的控件上空格就落空。本过滤器装在 MainWindow 之前,
// 对话框里的空格先被这里处理;主窗口过滤器也已把对话框/弹窗的键整体放行,两道不冲突。
class DialogKeyFilter : public QObject {
public:
    using QObject::QObject;

    // 空格在这些控件里有本职用途(打字/弹下拉/列表选择/菜单),一律让路。
    // 文本类判据与主窗口过滤器共用 keytarget.h;列表项这里是主窗口口径之外
    // 多加的:对话框里选中列表时空格属于列表,不属于"确认"。
    static bool spaceReserved(QObject* o) {
        return textInputWidget(o) || qobject_cast<QAbstractItemView*>(o);
    }
    static QDialog* dialogOf(QObject* obj) {
        QWidget* w = qobject_cast<QWidget*>(obj);
        while (w) {
            if (auto* d = qobject_cast<QDialog*>(w)) return d;
            w = w->parentWidget();
        }
        return nullptr;
    }
    // 对话框的"肯定"按钮:消息框取 Yes(没有则退回默认按钮,OK 型信息框即 OK);
    // 普通对话框取按钮箱的 Yes/Ok,否则**明确**被设成默认的那个按钮。
    // 绝不再兜底 autoDefault:实测(Qt 6.5.3,cache/tmp/space_confirm_test.cpp)
    // QDialog 里每个 QPushButton 都是 autoDefault=true、isDefault=false,
    // 那条兜底等于"点创建最早的按钮"——缓存维护窗里那是"删除选中目录条目"。
    // 没有明确默认就返回空:空格在那种窗里落空,比点错按钮好。
    static QAbstractButton* confirmButtonOf(QDialog* dlg) {
        if (auto* mb = qobject_cast<QMessageBox*>(dlg)) {
            if (QAbstractButton* yes = mb->button(QMessageBox::Yes)) return yes;
            return mb->defaultButton();
        }
        if (auto* bb = dlg->findChild<QDialogButtonBox*>()) {
            for (QDialogButtonBox::StandardButton sb :
                 { QDialogButtonBox::Yes, QDialogButtonBox::Ok })
                if (QAbstractButton* b = bb->button(sb)) return b;
        }
        const auto btns = dlg->findChildren<QPushButton*>();
        for (QPushButton* b : btns) if (b->isDefault()) return b;
        return nullptr;
    }

protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        const QEvent::Type type = ev->type();
        if (type != QEvent::Show && type != QEvent::KeyPress) return false;

        auto* w = qobject_cast<QWidget*>(obj);
        if (!w) return false;

        if (type == QEvent::Show) {
            // Yes/No 消息框:默认=Yes,Esc=No。其余对话框不代设默认,交给原生。
            if (auto* box = qobject_cast<QMessageBox*>(w)) {
                QAbstractButton* yes = box->button(QMessageBox::Yes);
                QAbstractButton* no  = box->button(QMessageBox::No);
                if (yes && no) {
                    box->setDefaultButton(QMessageBox::Yes);
                    box->setEscapeButton(QMessageBox::No);
                }
            }
            return false;
        }

        auto* ke = static_cast<QKeyEvent*>(ev);
        if (ke->key() != Qt::Key_Space || ke->isAutoRepeat()) return false;
        if (ke->modifiers() & (Qt::ControlModifier | Qt::AltModifier
                               | Qt::MetaModifier)) return false;
        if (qobject_cast<QAbstractButton*>(w))
            return false;   // 焦点在按钮:交给原生 —— 空格点的是"聚焦的那个"
                            //(Tab 到 No 时空格=No;旧版强制点 Yes 是错的)
        if (spaceReserved(w)) return false;
        QDialog* dlg = dialogOf(obj);
        if (!dlg) return false;   // 只管对话框;主窗口的空格另有用途
        QAbstractButton* target = confirmButtonOf(dlg);
        if (!target) return false;
        // 排队触发:同步点击会在按键事件派发中途销毁对话框。
        // animateClick 先呈现约 100ms 按下态再触发 —— 空格确认有可见反馈
        QPointer<QAbstractButton> btn(target);
        QTimer::singleShot(0, this, [btn]() { if (btn) btn->animateClick(); });
        return true;
    }
};

// "仅允许运行一个实例":第二个进程把命令行路径转交给已运行实例后退出
static const QString kSingleServer = QStringLiteral("GazeSingleInstance");

// 把命令行路径转交给已运行的实例。
// 返回 true  = 对方已把数据接走,调用方可以直接退出;
// 返回 false = 没有实例在听,或它忙到期限都没腾出手 —— 调用方自己开窗。
// 实测(Qt 6.5.3 / Windows 命名管道,cache/tmp/ipc_server_test.cpp):
//   · 事实A/B:首实例事件循环被占住时,connectToServer 照样 0ms 成功(连接由内核
//     完成),但 waitForBytesWritten(500) 返回 false、26 字节全留在本进程待发。
//     旧代码不看这个返回值就 return 0 —— 数据随进程一起没了,用户看到的是
//     "双击图片毫无反应"(比开两个窗口更糟)。所以 300ms 探测超时不是软肋,
//     "写完就走"才是。
//   · 事实G:在本地事件循环里等 bytesToWrite() 归零,首实例一恢复就落地(实测
//     约 980ms),路径完整送达,无需改服务端读取逻辑。
static bool handOffToRunningInstance(const QStringList& paths) {
    QLocalSocket probe;
    probe.connectToServer(kSingleServer);
    if (!probe.waitForConnected(300)) return false;

    for (const QString& p : paths)
        probe.write((p + "\n").toUtf8());

    QEventLoop loop;
    QTimer deadline;
    deadline.setSingleShot(true);
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    QObject::connect(&probe, &QLocalSocket::bytesWritten, &probe, [&]() {
        if (probe.bytesToWrite() == 0) loop.quit();
    });
    deadline.start(2000);
    if (probe.bytesToWrite() > 0) loop.exec();

    if (probe.bytesToWrite() > 0) {   // 首实例 2s 没接手:别静默消失
        Logger::event(QStringLiteral("single-instance handoff timed out; opening own window"));
        return false;
    }
    probe.disconnectFromServer();
    return true;
}

// 开始监听"第二个实例"的转交。返回 nullptr = 监听没起来(调用方保持普通多实例行为)
static QLocalServer* startSingleInstanceListener(QWidget* w) {
    QLocalServer::removeServer(kSingleServer);
    auto* server = new QLocalServer(qApp);
    if (!server->listen(kSingleServer)) {
        // 实测(事实E):同名第二个 listen() 在 Windows 上仍返回 true(命名管道允许多
        // 实例),所以这里失败绝不是"已有实例在跑",而是真出错(权限等)。
        // 不写下来就是个静默黑洞:设置勾了、看着一切正常、转交永远没人接。
        Logger::event(QStringLiteral("single-instance listen failed: ")
                      + server->errorString());
        delete server;
        return nullptr;
    }
    QObject::connect(server, &QLocalServer::newConnection, w, [server, w]() {
        // 一有连接就唤起:第二个实例可能一个字节都不带(只是"再开一次"没给路径),
        // 而 readyRead 没数据时根本不会发 —— 实测原写法这种情形 raise 次数=0,
        // 即"打开第二个窗口没反应,第一个也不前置"。
        w->raise();
        w->activateWindow();
        QLocalSocket* s = server->nextPendingConnection();
        if (!s) return;
        // 每条连接各发一次 newConnection(实测 5 个客户端排队 = 5 次),
        // 所以一次一个 nextPendingConnection() 不丢东西,不必排空。
        QObject::connect(s, &QLocalSocket::readyRead, s, [w, s]() {
            const QList<QByteArray> lines = s->readAll().split('\n');
            for (const QByteArray& ln : lines) {
                const QString p = QDir::fromNativeSeparators(
                    QString::fromUtf8(ln).trimmed());
                if (p.isEmpty() || !QFileInfo::exists(p)) continue;
                if (QFileInfo(p).isDir())
                    QMetaObject::invokeMethod(w, "navigateTo", Q_ARG(QString, p));
                else
                    QMetaObject::invokeMethod(w, "revealFile", Q_ARG(QString, p));
            }
        });
        QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
    });
    return server;
}

int main(int argc, char *argv[]) {
    // #101 AV1 黑屏:FFmpeg 原生 av1 解码器只是硬解外壳,拿不到 hwaccel 不会回退软解
    // (实测 `ffmpeg -c:v av1 -i av1.mp4` exit=69 并打印 "platform doesn't support
    // hardware accelerated AV1 decoding";本机 RTX 2060 是 Turing,无 AV1 硬解)。
    // 唯一的软解通路是随 avcodec 一起换入的 libdav1d,而它要求 Qt 别递硬件设备:
    // 同一份新 avcodec,默认(允许硬解)零帧、none 满帧,两件事缺一不可。
    // 代价:所有编码都走软解——实测预览场景首帧反而快 2~3 倍(1080p H.264 306→58ms)。
    // 位置要求:FFmpeg 插件首次载入时读一次,故必须在 QApplication 之前。
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES", "none");

    QApplication app(argc, argv);
    app.setApplicationName("Gaze");
    app.setApplicationDisplayName("Gaze");
    app.setWindowIcon(QIcon(":/gaze.png"));
    static DialogKeyFilter s_dialogKeyFilter;
    app.installEventFilter(&s_dialogKeyFilter);

    // 卡死/崩溃诊断:事件日志+看门狗+minidump(logger.h);3s 心跳证明 GUI 活着
    Logger::init();
    QTimer touchTimer;
    QObject::connect(&touchTimer, &QTimer::timeout, []() { Logger::touch(); });
    touchTimer.start(3000);
    QObject::connect(&app, &QCoreApplication::aboutToQuit,
                     []() { Logger::event(QStringLiteral("session end")); });

    auto cliPaths = []() {
        QStringList out;
        const QStringList args = QCoreApplication::arguments();
        for (int i = 1; i < args.size(); ++i) {
            const QString a = args[i].trimmed();
            if (!a.isEmpty() && !a.startsWith(QLatin1Char('-'))) out << a;
        }
        return out;
    };

    if (AppSettings::instance().get("General/singleInstance", false).toBool()
        && handOffToRunningInstance(cliPaths()))
        return 0;
    Logger::boot("handoff-probe");

    // 全局 QSS 收编进 Theme::appQss()(#96):Theme::T 让同一张样式表在深/浅两档间取值,
    // Appearance/theme 决定,重启生效。样式文本与占位符表见 theme.cpp
    Theme::init();
    app.setStyleSheet(Theme::appQss());

    Logger::boot("qss");
    MainWindow w;
    Logger::boot("mw-ctor");

    // 单实例监听跟着 General/singleInstance 走:旧写法只在启动时读一次,
    // 设置页那个勾必须重启才生效(等于半个假开关)。changed() 每次设置写入都会发,
    // 这里借它做即时起停。
    QPointer<QLocalServer> singleServer;
    auto applySingleInstance = [&w, &singleServer]() {
        const bool on =
            AppSettings::instance().get("General/singleInstance", false).toBool();
        if (on && !singleServer) {
            singleServer = startSingleInstanceListener(&w);
            if (singleServer)
                Logger::event(QStringLiteral("single-instance listener up"));
        } else if (!on && singleServer) {
            Logger::event(QStringLiteral("single-instance listener off"));
            singleServer->close();
            singleServer->deleteLater();
            singleServer = nullptr;
        }
    };
    applySingleInstance();
    Logger::boot("single-listen");
    QObject::connect(&AppSettings::instance(), &AppSettings::changed,
                     &w, [applySingleInstance]() { applySingleInstance(); });

    w.show();
    Logger::boot("show");

    // Cache/checkOnStartup:启动后延后一会儿再校验缓存完整性 ——
    // 与首屏缩略图请求错开,避免一上来就抢 I/O(校验在后台线程跑)
    QTimer::singleShot(4000, &w, []() {
        if (AppSettings::instance().get("Cache/checkOnStartup", false).toBool())
            Thumbnailer::instance().verifyCache();
    });

    return app.exec();
}
