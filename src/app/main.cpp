#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QDir>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMessageBox>
#include <QAbstractButton>
#include <QEvent>
#include <QKeyEvent>
#include <QEventLoop>
#include <QTimer>
#include <QPointer>
#include <QPointer>
#include "mainwindow.h"
#include "constants.h"
#include "keytarget.h"
#include "keytarget.h"
#include "settings.h"
#include "thumbnailer.h"
#include "logger.h"
#include "logger.h"

// Yes/No 对话框键盘语义:Space/Enter → Yes,Esc → No
// 仅在同时存在 Yes 与 No 按钮时介入,不影响 OK/Cancel 等其他场景。
class YesNoKeyFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        // 必须先筛事件类型再碰 button():QMessageBox 构造期间 Qt 就会向这个
        // 半成品对象派发事件(子控件 setParent→sendEvent),此时内部
        // QDialogButtonBox 还没建,box->button() 直接空指针。
        const QEvent::Type type = ev->type();
        if (type != QEvent::Show && type != QEvent::KeyPress) return false;

        auto* box = qobject_cast<QMessageBox*>(obj);
        if (!box) return false;
        QAbstractButton* yes = box->button(QMessageBox::Yes);
        QAbstractButton* no  = box->button(QMessageBox::No);
        if (!yes || !no) return false;
        if (type == QEvent::Show) {
            box->setDefaultButton(QMessageBox::Yes);
            box->setEscapeButton(QMessageBox::No);
            QWidget* fw = box->focusWidget();
            qWarning("[filter] Show: focus=%s isYes=%d isNo=%d",
                     fw ? fw->metaObject()->className() : "null",
                     fw == box->button(QMessageBox::Yes),
                     fw == box->button(QMessageBox::No));
            return false;
        }
        if (type == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            qWarning("[filter] KeyPress on box: key=0x%x", ke->key());
            if (ke->key() == Qt::Key_Space) {
                QWidget* focused = box->focusWidget();
                // 焦点在按钮或box本身时让Space=Yes;若落在输入控件则让给它
                if (!focused
                    || qobject_cast<QAbstractButton*>(focused)
                    || focused == box) {
                    // 排队执行:click() 会关闭对话框,同步调用等于在事件派发中销毁接收者
                    QTimer::singleShot(0, yes, [yes]() { yes->click(); });
                    return true;
                }
            }
        }
        return false;
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
    Logger::boot("handoff-probe");
    Logger::boot("handoff-probe");
    Logger::boot("handoff-probe");
    Logger::boot("handoff-probe");

    // 全局 QSS 收编进 Theme::appQss()(#96):Theme::T 让同一张样式表在深/浅两档间取值,
    // Appearance/theme 决定,重启生效。样式文本与占位符表见 theme.cpp
    Theme::init();
    app.setStyleSheet(Theme::appQss());

    Logger::boot("qss");
    Logger::boot("qss");
    Logger::boot("qss");
    MainWindow w;
    Logger::boot("mw-ctor");
    Logger::boot("mw-ctor");
    Logger::boot("mw-ctor");
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
    QObject::connect(&AppSettings::instance(), &AppSettings::changed,
                     &w, [applySingleInstance]() { applySingleInstance(); });

    // ── 临时诊断:GAZE_SELFTEST=s 时复现"选中第一项 → 按 S → 确认框按 Space"。
    //    走的是与真实按键同一条事件派发链,但不依赖 OS 输入合成。查完删除。
    if (qEnvironmentVariableIsSet("GAZE_SELFTEST")) {
        // GUI 子系统程序的 qWarning 走 OutputDebugString,重定向 stderr 抓不到,
        // 这里把诊断输出直接落到 selftest.log。临时诊断,查完删除。
        static QFile slog(QCoreApplication::applicationDirPath() + "/selftest.log");
        slog.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text);
        qInstallMessageHandler([](QtMsgType, const QMessageLogContext&,
                                  const QString& msg) {
            slog.write(QDateTime::currentDateTime().toString("HH:mm:ss.zzz ").toUtf8());
            slog.write(msg.toUtf8());
            slog.write("\n");
            slog.flush();
        });
        qWarning("[selftest] mode=%s", qPrintable(QString::fromLocal8Bit(qgetenv("GAZE_SELFTEST"))));
        w.resize(1600, 900);   // 用接近真实使用的窗口尺寸,否则列数太少测不出东西
    }
    if (qgetenv("GAZE_SELFTEST") == "scroll") {
        // 进程内模拟快速拖动滚动条 + 画布几何自检
        // 构造函数末尾的 applyLastLayout 会覆盖更早的 resize,所以在显示之后再放大窗口
        QTimer::singleShot(1200, &w, [&w]() { w.resize(1600, 900); });
        QTimer::singleShot(1500, &w, [&w]() { w.selftestFastScroll(); });
        QTimer::singleShot(20000, &w, []() {
            qWarning("[selftest] scroll watchdog fired");
            qApp->quit();
        });
    } else if (const QByteArray m = qgetenv("GAZE_SELFTEST");
               m == "s" || m == "sb") {
        // s  = Space 投给焦点按钮(走 Qt 原生按钮激活)
        // sb = Space 投给对话框本身(走 YesNoKeyFilter 的排队 click() 分支)
        const bool toBox =
            qgetenv("GAZE_SELFTEST") == "sb";
        QTimer::singleShot(1500, &w, [&w]() {
            qWarning("[selftest] press S");
            w.selftestPressKey(Qt::Key_S);
        });
        QTimer::singleShot(2600, &w, [&w, toBox]() {
            int boxes = 0;
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget* t : tops) {
                auto* box = qobject_cast<QMessageBox*>(t);
                if (!box || !box->isVisible()) continue;
                ++boxes;
                QWidget* fw = box->focusWidget();
                QWidget* target = toBox ? static_cast<QWidget*>(box)
                                        : (fw ? fw : static_cast<QWidget*>(box));
                qWarning("[selftest] confirm box shown, focus=%s isYes=%d isNo=%d "
                         "default=%d -> post Space to %s",
                         fw ? fw->metaObject()->className() : "null(box)",
                         fw == box->button(QMessageBox::Yes),
                         fw == box->button(QMessageBox::No),
                         box->defaultButton() == box->button(QMessageBox::Yes),
                         target->metaObject()->className());
                QCoreApplication::postEvent(target,
                    new QKeyEvent(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier));
                QCoreApplication::postEvent(target,
                    new QKeyEvent(QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier));
            }
            if (!boxes) qWarning("[selftest] no confirm box (silent delete path)");
        });
        QTimer::singleShot(3400, &w, []() {
            int stillOpen = 0;
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget* t : tops) {
                auto* box = qobject_cast<QMessageBox*>(t);
                if (box && box->isVisible()) ++stillOpen;
            }
            qWarning("[selftest] confirm boxes still open after Space = %d "
                     "(0 = Space really accepted)", stillOpen);
        });
        QTimer::singleShot(3400, &w, []() {
            int stillOpen = 0;
            const auto tops = QApplication::topLevelWidgets();
            for (QWidget* t : tops) {
                auto* box = qobject_cast<QMessageBox*>(t);
                if (box && box->isVisible()) ++stillOpen;
            }
            qWarning("[selftest] confirm boxes still open after Space = %d "
                     "(0 = Space really accepted)", stillOpen);
        });
        QTimer::singleShot(4500, &w, []() {
            qWarning("[selftest] survived 4.5s without crashing");
            qApp->quit();
        });
    }

    w.show();

    // Cache/checkOnStartup:启动后延后一会儿再校验缓存完整性 ——
    // 与首屏缩略图请求错开,避免一上来就抢 I/O(校验在后台线程跑)
    QTimer::singleShot(4000, &w, []() {
        if (AppSettings::instance().get("Cache/checkOnStartup", false).toBool())
            Thumbnailer::instance().verifyCache();
    });

    return app.exec();
}
