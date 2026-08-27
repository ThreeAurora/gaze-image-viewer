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
#include "mainwindow.h"
#include "settings.h"

// Yes/No 对话框键盘语义:Space/Enter → Yes,Esc → No
// 仅在同时存在 Yes 与 No 按钮时介入,不影响 OK/Cancel 等其他场景。
class YesNoKeyFilter : public QObject {
public:
    using QObject::QObject;
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        auto* box = qobject_cast<QMessageBox*>(obj);
        if (!box) return false;
        QAbstractButton* yes = box->button(QMessageBox::Yes);
        QAbstractButton* no  = box->button(QMessageBox::No);
        if (!yes || !no) return false;
        if (ev->type() == QEvent::Show) {
            box->setDefaultButton(yes);
            box->setEscapeButton(no);
            return false;
        }
        if (ev->type() == QEvent::KeyPress) {
            auto* ke = static_cast<QKeyEvent*>(ev);
            if (ke->key() == Qt::Key_Space) {
                QWidget* focused = box->focusWidget();
                // 焦点在按钮或box本身时让Space=Yes;若落在输入控件则让给它
                if (!focused
                    || qobject_cast<QAbstractButton*>(focused)
                    || focused == box) {
                    yes->click();
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
    static YesNoKeyFilter s_yesNoFilter;
    app.installEventFilter(&s_yesNoFilter);

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

    // 本实例自带的路径:统一交给 MainWindow 处理(mainwindow 构造函数读取 argv)
    w.show();

    // 首个实例经"打开方式/右键浏览"启动时,也要把自己登记为监听方并处理路径
    if (server) {
        // 转发路径已在 MainWindow 构造时消费;此处仅保持 server 存活
    }
    return app.exec();
}
