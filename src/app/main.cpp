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
#include <QTimer>
#include <QPointer>
#include "mainwindow.h"
#include "constants.h"
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

    const bool singleInstance =
        AppSettings::instance().get("General/singleInstance", false).toBool();
    if (singleInstance) {
        QLocalSocket probe;
        probe.connectToServer(kSingleServer);
        if (probe.waitForConnected(300)) {
            for (const QString& p : cliPaths())
                probe.write((p + "\n").toUtf8());
            probe.waitForBytesWritten(500);
            probe.disconnectFromServer();
            return 0;
        }
    }
    Logger::boot("handoff-probe");

    // 全局 QSS 收编进 Theme::appQss()(#96):Theme::T 让同一张样式表在深/浅两档间取值,
    // Appearance/theme 决定,重启生效。样式文本与占位符表见 theme.cpp
    Theme::init();
    app.setStyleSheet(Theme::appQss());

    MainWindow w;

    QLocalServer* server = nullptr;
    if (singleInstance) {
        QLocalServer::removeServer(kSingleServer);
        server = new QLocalServer(&app);
        if (server->listen(kSingleServer)) {
            QObject::connect(server, &QLocalServer::newConnection, &w, [&w, server]() {
                QLocalSocket* s = server->nextPendingConnection();
                if (!s) return;
                QObject::connect(s, &QLocalSocket::readyRead, s, [&w, s]() {
                    const QList<QByteArray> lines = s->readAll().split('\n');
                    for (const QByteArray& ln : lines) {
                        const QString p = QDir::fromNativeSeparators(
                            QString::fromUtf8(ln).trimmed());
                        if (p.isEmpty() || !QFileInfo::exists(p)) continue;
                        if (QFileInfo(p).isDir())
                            QMetaObject::invokeMethod(&w, "navigateTo", Q_ARG(QString, p));
                        else
                            QMetaObject::invokeMethod(&w, "revealFile", Q_ARG(QString, p));
                    }
                    w.raise();
                    w.activateWindow();
                });
                QObject::connect(s, &QLocalSocket::disconnected, s, &QObject::deleteLater);
            });
        }
    }

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
