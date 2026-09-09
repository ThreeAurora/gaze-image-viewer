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
#include <functional>
#include <windows.h>
#include "mainwindow.h"
#include "constants.h"
#include "keytarget.h"
#include "settings.h"
#include "thumbnailer.h"
#include "logger.h"
#include "everything_engine.h"   // #251:退出清理自带 Everything 实例(es -instance gaze -exit)

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

// ── 启动首帧闸门(2026-09-05「启动先弹窗」报告根治;2026-09-06 加兜底)──
// 旧链:show 后 singleShot(0) 里先恢复不透明、再跑媒体栈预热(FFmpeg 后端首载
// 同步阻塞 1.6s+)。而首帧 PAINT 还排在事件队列里 —— GUI 线程被预热占住,
// paint 根本跑不了,用户看到一扇纯白空窗顶足 1~2 秒才出 UI(probe_startup_win
// 连拍 cap_001@SHOW+37ms 全白、cap_005@+1.1s 才画好),正是"先弹窗"本体。
// 首帧透明(b91b4711)的防闪意图被后来同回调里加入的预热(aea7b3ac)冲垮。
// 现改为 PAINT 事件驱动:第一帧在仍不可见时真画完,再放行"恢复不透明+预热+
// 恢复预览"——窗口出现的瞬间就是画好的深色 UI,白窗期=0。
// 兜底超时(2026-09-06 用户报"任务栏有图标、窗口三秒不出"):恢复上次选中
// 是视频时,装载事件把 PAINT 挤到队尾,纯事件驱动会让透明窗干等。加 1.5s
// 超时强制放行——最坏情况退回"短暂白/空窗",决不无限隐身。
class FirstPaintGate : public QObject {
public:
    std::function<void()> fire;
    void armWithTimeout(int ms, QObject* owner) {
        m_timer = new QTimer(this);
        m_timer->setSingleShot(true);
        connect(m_timer, &QTimer::timeout, this, [this]() { release(true); });
        m_timer->start(ms);
        m_owner = owner;
    }
protected:
    bool eventFilter(QObject* obj, QEvent* ev) override {
        if (ev->type() != QEvent::Paint || m_done) return false;
        release(false);
        return false;
    }
private:
    // byTimeout=超时兜底放行(PAINT 被装载事件挤到队尾)/=首帧真画完放行。
    // 打点留档:灰四边形排查需要知道"窗口何时可见、由哪条路放行"。
    void release(bool byTimeout) {
        if (m_done) return;
        m_done = true;
        if (m_timer) m_timer->stop();
        QObject* owner = m_owner ? m_owner : parent();
        if (!owner) owner = this;
        removeEventFilterFrom(owner);
        Logger::event(QStringLiteral("first-frame gate: released by %1, age=%2ms")
                          .arg(byTimeout ? QStringLiteral("timeout")
                                         : QStringLiteral("paint"))
                          .arg(Logger::processAgeMs()));
        // singleShot 一拍:让本帧 paint 先走完(若还在队列里),再执行阻塞预热
        QTimer::singleShot(0, owner, fire);
    }
    void removeEventFilterFrom(QObject*) {}   // 过滤器挂在 w 上,由外部管理
    QTimer* m_timer = nullptr;
    QObject* m_owner = nullptr;
    bool m_done = false;
};

// ── 启动防闪史(2026-09-03 深夜末段5 定案,StartupWindowGuard 已删除)──
// 三代守卫(主窗透明→按类名钳→按尺寸钳)针对的"160x28 图标拥有者小窗闪现"
// 经 probe_flash 外挂钩子实测(闪窗事件全录,flash_noguard.log)并不存在:
// 无守卫启动全程唯一可见窗口就是主窗自己,其余全是 vis=0 的内部窗。
// 守卫反而在 3 秒钳制窗内两次把弹窗压出屏(隐形模态:点击全死、任务栏
// 关不掉,2026-09-03 夜用户亲历)。396x65 黑条闪窗的真正根因是无父
// SortHeader 先 setVisible 再 addWidget,已按"先挂布局后设可见"根治
// (mainwindow.cpp)。防闪手段保留:主窗首帧透明 + show 后恢复(下方)。

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
    // #101 AV1 黑屏的历史教训(保留备查):FFmpeg 原生 av1 解码器只是硬解外壳,
    // 拿不到 hwaccel 不会回退软解(实测 "platform doesn't support hardware
    // accelerated AV1 decoding";RTX 2060 是 Turing,无 AV1 硬解),当时为救 AV1
    // 全盘禁了硬解(QT_FFMPEG_DECODING_HW_DEVICE_TYPES=none)。
    // 2026-09-08 重测(探针实测,Qt 6.8.3 + RTX 2060):
    //   1080p H264 首帧  none=58ms  vs d3d11va=308ms(浏览首帧软解仍占优)
    //   4K HEVC     首帧  none=135ms vs d3d11va=393ms,帧率两者都满帧
    //   8K HDR HEVC      软解 Stalled 卡死(用户实测 pos 不进),硬解是唯一出路
    //   AV1              d3d11va 零帧复现("Failed setup for format d3d11")
    // 结论:按设置开关(默认 d3d11va)——4K/8K/HDR 用户受益;AV1 视频、
    // 无硬解 GPU 的用户在设置里关掉即可。AV1 零帧的自动转码自救另行排期。
    // 位置要求:FFmpeg 插件首次创建 QMediaPlayer 时才加载,故 QApplication
    // 之后、主窗构造之前读 ini 并 qputenv 仍然赶得上(与旧注释相反)。
    QApplication app(argc, argv);
    app.setApplicationName("Gaze");
    app.setApplicationDisplayName("Gaze");
    app.setWindowIcon(QIcon(":/Gaze.png"));

    // 视频硬解开关(设置→视频):默认开。必须在任何 QMediaPlayer 创建之前生效。
    qputenv("QT_FFMPEG_DECODING_HW_DEVICE_TYPES",
            AppSettings::instance().get("Video/hardwareDecoding", true).toBool()
                ? QByteArrayLiteral("d3d11va")
                : QByteArrayLiteral("none"));

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
                     []() {
                         Logger::event(QStringLiteral("session end"));
                         // 退出收口(2026-09-05):此刻 QCoreApplication 仍存活,
                         // 缩略图工作线程的收尾(写库/读设置)才是安全的;放任到
                         // 静态析构,曾致工作线程在 app 死后继续跑+锁着的锁被
                         // 销毁,waitForDone 永挂=关窗后进程残留(WerFault 实锤)。
                         Thumbnailer::instance().shutdown();
                         // #251 退出清理:只停 Gaze 自带的 Everything 独立实例
                         // (es -instance gaze -exit),用户机器上的系统版 Everything
                         // 完全不受影响。es 进程毫秒级返回,不拖慢退出。
                         ev_impl::shutdown();
                     });

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
    // StartupWindowGuard 已提前到 QApplication 之后安装(见 main 开头注释)。
    w.setWindowOpacity(0.0);
    // 2026-09-07 修:媒体栈预热提前到 show 之前(原在首帧 gate.fire 里):
    // dir-scan done 常在 show 后几十 ms 就兑现"上次选中/首项=视频",
    // loadFile 若抢在池线程载完 DLL 前建 QMediaPlayer,GUI 同步载 66MB
    // avcodec 冻结 3~4 秒——期间 PAINT 与 1.5s 超时兜底全堵在事件队列里
    // 发不出,即"任务栏有图标窗口三秒不出"+灰块期的真身(日志实锤:
    // show@2888ms→dir-scan done@2946ms→gate released@6511ms,中间被
    // QMediaPlayer 首建占满)。现在预热与首帧/装载并行;万一视频装载仍
    // 先到,setupPlayer 的媒体门闩会挂起,就绪后自动重放。
    w.warmUpPreviewMedia();
    w.show();
    // 放行时机=首帧真画完(上方 FirstPaintGate);恢复上次选中在放行后:
    // QVideoWindow(独立顶层 HWND)必须等主窗就位再装载,防孤立映射一帧
    FirstPaintGate gate;
    gate.fire = [&w]() {
        w.setWindowOpacity(1.0);
        w.restoreStartupPreview();
    };
    w.installEventFilter(&gate);
    gate.armWithTimeout(1500, &w);   // PAINT 迟迟不来时的强制放行
    Logger::boot("show");

    // #251(2026-09-09 用户令"Everything 引擎就是为了瞬间文件夹大小"):
    // 引擎改为**随 Gaze 常驻** —— 独立实例建好 NTFS 索引后一直挂着,文件夹
    // 大小统计/全盘快搜随时秒回。原"懒启动"只在打开快搜框才拉,用户平时
    // 不进搜索框 → 目录统计退化回内置递归"统计中",与"瞬间统计"相悖。
    // 延迟 3 秒静默拉起:避开首屏缩略图 I/O 高峰;异步零阻塞,失败不影响
    // 任何现有路径(查询自动回退内置引擎)。退出收口就在上方 aboutToQuit。
    QTimer::singleShot(3000, []() {
        ev_impl::ensureRunning(nullptr, nullptr);   // 拉引擎,结果不需要
    });

    // Cache/checkOnStartup:启动后延后一会儿再校验缓存完整性 ——
    // 与首屏缩略图请求错开,避免一上来就抢 I/O(校验在后台线程跑)
    QTimer::singleShot(4000, &w, []() {
        if (AppSettings::instance().get("Cache/checkOnStartup", false).toBool())
            Thumbnailer::instance().verifyCache();
    });

    return app.exec();
}
