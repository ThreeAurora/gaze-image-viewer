#include <QApplication>
#include <QIcon>
#include <QFileInfo>
#include <QFile>
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
#include <QTranslator>
#include <QLocale>
#include <QLibraryInfo>
#include <QDateTime>
#include <QAbstractNativeEventFilter>
#include <windows.h>
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

// ── 启动防闪 #2(2026-09-03 复盘两轮后的最终修法)──
// 两轮旧解法的错都在**识别口径**上:
//   · 第一版只把主窗口整体透明 —— 透明盖得住主窗自身的第一帧,盖不住
//     Qt 为顶层窗口自动建的 160x28「图标拥有者」小窗:它是个独立顶层
//     HWND,与预览的是图片还是视频无关,首次 show 落位前会在原位(-32000
//     之前)短暂映射一帧 —— 就是用户看到的"一闪而过的、只有空标题栏的
//     小窗",不分图片/视频都能复现。
//   · 第二版在原生消息层按**类名**全钳 —— Qt6 所有顶层窗口(MainWindow/
//     QDialog/QVideoWindow)共用 Win32 类名 "Qt683QWindowIcon",于是视频
//     输出窗、设置等弹出框一起被送出屏幕(弹窗不可见但模态照常拦鼠标,
//     只能强杀进程)。只能整版回退。
// 可靠区分点其实在**尺寸**:图标小窗固定 160x28(专伺候任务栏/Alt-Tab);
// 视频窗/对话框是实物内容尺寸(几百 x 几百起)。第三版只钳"小尺寸"的
// Qt683QWindowIcon,大窗一概放行 —— 同类名不再彼此误伤。
class StartupWindowGuard : public QAbstractNativeEventFilter {
public:
    bool nativeEventFilter(const QByteArray&, void* message, qintptr*) override {
        auto* msg = static_cast<MSG*>(message);
        // 2026-09-03 夜补漏:只在启动头 3 秒内钳(防启动闪现),此后语言切换
        // 等模态对话框弹出时不再误伤其图标小窗 —— 此前全程钳制导致"隐形
        // 模态窗盖住点击、任务栏关不掉"。
        if (!msg || msg->message != WM_WINDOWPOSCHANGING || !msg->hwnd)
            return false;
        static const qint64 startMs
            = QDateTime::currentMSecsSinceEpoch();
        if (QDateTime::currentMSecsSinceEpoch() - startMs > 3000)
            return false;
        wchar_t cls[64];
        const int n = GetClassNameW(msg->hwnd, cls, 64);
        if (n <= 0) return false;
        const QString clsName = QString::fromWCharArray(cls, n);
        auto* wp = reinterpret_cast<WINDOWPOS*>(msg->lParam);
        // 只拦 Qt683QWindowIcon 且尺寸像"图标拥有者小窗"的:窗口照建照活
        //(任务栏/Alt-Tab 图标由它托管),只是落位一律压到屏外,闪现从根上消失。
        if (clsName == QStringLiteral("Qt683QWindowIcon")
            && wp->cx <= 200 && wp->cy <= 80) {
            if (wp->x > -1000 || wp->y > -1000) {
                wp->x = -32000;
                wp->y = -32000;
            }
            return false;   // 只改位置:尺寸/显示标志不动,继续走默认流程
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
    app.setWindowIcon(QIcon(":/Gaze.png"));

    // 旧版配置迁移: gaze.ini -> Gaze.ini (仅当新名不存在而旧名存在时一次性改名)
    {
        const QString oldIni = QCoreApplication::applicationDirPath() + QStringLiteral("/gaze.ini");
        const QString newIni = QCoreApplication::applicationDirPath() + QStringLiteral("/Gaze.ini");
        if (QFileInfo::exists(oldIni) && !QFileInfo::exists(newIni))
            QFile::rename(oldIni, newIni);
    }

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

    // ── 界面语言(2026-09-03 国际化)──
    // General/language:system=跟随系统(默认)| zh=简体中文 | en=English。
    // 跟随系统 = 非中文系统一律进英文 —— 老外装上即英文界面,不用找开关;
    // 手动切换走菜单栏"语言"菜单写入本键,重启后生效(整套 UI 在构造期解析)。
    // 英文译文装在 exe 旁 gaze_en.qm(由 translations/i18n_build.py 生成)。
    // 除应用自己的译文,z 模式/ en 模式分别加载 qtbase_zh_CN / qtbase_en:
    // Qt 标准按钮、QFileDialog 等内置部件的文字按系统区域自动加载,与所选
    // 界面语言不一致会混出"中文界面+英文按钮"(中文系统强制 English 时),
    // 这里手动装一份后装的译者会盖过自动那份(后装优先),保证两厢一致。
    // --restart 是语言切换后的自重启标记:携带它跳过单实例握手,避开
    // "新进程把路径转交给旧进程、旧进程随即退出"的竞态。
    const bool relaunched = QCoreApplication::arguments()
        .contains(QStringLiteral("--restart"));
    {
        QString lang = AppSettings::instance().get("General/language", "system").toString();
        if (lang == QLatin1String("system")) {
            const QLocale::Language sys = QLocale::system().language();
            lang = (sys == QLocale::Chinese) ? QStringLiteral("zh")
                                             : QStringLiteral("en");
        }
        static QTranslator s_qtbase;   // 覆盖 Qt 内置部件(标准按钮等)的自动译文
        const QString qtPath = QLibraryInfo::path(QLibraryInfo::TranslationsPath);
        QString qtbaseFile = (lang == QLatin1String("zh"))
            ? QStringLiteral("qtbase_zh_CN") : QStringLiteral("qtbase_en");
        if (s_qtbase.load(qtbaseFile, qtPath)) app.installTranslator(&s_qtbase);
        if (lang == QLatin1String("en")) {
            static QTranslator s_en;
            const QString qm = QCoreApplication::applicationDirPath()
                + QStringLiteral("/gaze_en.qm");
            const bool loaded = s_en.load(qm);
            Logger::event(QStringLiteral("i18n: lang=en qm=%1 loaded=%2")
                          .arg(qm, loaded ? "yes" : "NO"));
            if (loaded)
                app.installTranslator(&s_en);
        } else {
            Logger::event(QStringLiteral("i18n: lang=%1 (source zh)").arg(lang));
        }
    }

    if (!relaunched
        && AppSettings::instance().get("General/singleInstance", false).toBool()
        && handOffToRunningInstance(cliPaths()))
        return 0;
    Logger::boot("handoff-probe");

    // 全局 QSS 收编进 Theme::appQss()(#96):Theme::T 让同一张样式表在深/浅两档间取值,
    // Appearance/theme 决定。设置页切换会走 Theme::notifyChanged() 重灌内联样式/缓存色,
    // 主题切换即时生效,无需重启(协议见 theme.h)。启动这里只做首帧一致性。
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

    // 启动防闪:主窗口首帧整体透明,show 落位后再恢复不透明;
    // 同一拍里恢复上次选中文件的预览 —— QVideoWindow(FFmpeg 后端的顶层
    // 视频输出窗)若在主窗显示前装载,会在屏幕上孤立映射一帧"闪框",
    // 必须等主窗口就位后再触发 loadFile(见 MainWindow::restoreStartupPreview)。
    // 另装 StartupWindowGuard:把 Qt 的 160x28「图标拥有者」小窗钳在屏外,
    // 该窗是独立顶层 HWND,透明度管不着它(定义处注释了识别口径)。
    static StartupWindowGuard s_startupGuard;
    app.installNativeEventFilter(&s_startupGuard);
    w.setWindowOpacity(0.0);
    w.show();
    QTimer::singleShot(0, &w, [&w]() {
        w.setWindowOpacity(1.0);
        w.restoreStartupPreview();
    });
    Logger::boot("show");

    // Cache/checkOnStartup:启动后延后一会儿再校验缓存完整性 ——
    // 与首屏缩略图请求错开,避免一上来就抢 I/O(校验在后台线程跑)
    QTimer::singleShot(4000, &w, []() {
        if (AppSettings::instance().get("Cache/checkOnStartup", false).toBool())
            Thumbnailer::instance().verifyCache();
    });

    return app.exec();
}
