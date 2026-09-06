#pragma once
// ═══════════════════════════════════════════════════════════
// 万象图搜(imgseek)客户端 —— 进程外集成
// imgseek 是独立 Python 服务(FastAPI,默认 127.0.0.1:8747)。
// Gaze 侧只发 HTTP,不导入任何推理代码。
//
// 线程纪律:GUI 路径一律走 ImgClient(QNetworkAccessManager+回调,不阻塞);
// 旧版在 GUI 线程用 QEventLoop 同步等待,服务冷启动时整窗冻结(#70 同类),
// 已废。httpGet/thumbBytes 同步版保留,仅限工作线程调用。
//
// 已核实端点(imgseek README,TODO_ALL §6.10):
//   GET /api/search?q=&model=&sort= → {"total":N,"results":[{id,path,filename,sources:[{type,score}]}]}
//   GET /api/thumb/<image_id>       → WebP 缩略图
//   GET /api/status                 → 只用于探活(任何 HTTP 应答=进程活着)
// 其余端点(/api/folders /api/scan /api/models/activate /api/retry)字段级
// schema 未落实,客户端只提供通用 get/post,不建 UI(不猜字段名)。
// ═══════════════════════════════════════════════════════════

#include "i18n.h"       // 错误消息使用点包 gazeTr
#include <functional>
#include <QString>
#include <QList>
#include <QPair>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QTimer>
#include <QEventLoop>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QSharedPointer>
#include <QThread>
#include <QDateTime>
#include "settings.h"
#include "toolpath.h"

namespace ImgSearch {

inline QString defaultDir() {
    // 万象图搜项目目录(与 Gaze 同级:projects/2026/08/)
    return QFileInfo(QCoreApplication::applicationDirPath()
                     + "/../../万象图搜").absoluteFilePath();
}
inline int port() {
    return AppSettings::instance().get("ImgSearch/port", 8747).toInt();
}
inline QUrl baseUrl() {
    return QUrl(QString("http://127.0.0.1:%1").arg(port()));
}

// ── 同步工具:仅限非 GUI 线程(缩略图工作线程) ──
inline QByteArray httpGet(const QUrl& url, int timeoutMs, int* status = nullptr) {
    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = nam.get(req);
    QEventLoop loop;
    QTimer::singleShot(timeoutMs, &loop, &QEventLoop::quit);
    QObject::connect(reply, &QNetworkReply::finished, &loop, &QEventLoop::quit);
    loop.exec();
    if (status) *status = reply->attribute(
        QNetworkRequest::HttpStatusCodeAttribute).toInt();
    QByteArray data = reply->readAll();
    reply->deleteLater();
    return data;
}

// 缩略图字节(WebP;id 为 imgseek 库内 image id)
inline QByteArray thumbBytes(int imageId, int timeoutMs = 5000) {
    return httpGet(baseUrl().toString() + QString("/api/thumb/%1").arg(imageId),
                   timeoutMs);
}

// ── 异步客户端:GUI 线程专用 ──
// 无自定义信号,故不挂 Q_OBJECT(纯 QObject 足以承载 connect 上下文)。
class ImgClient : public QObject {
public:
    using Callback = std::function<void(int status, const QByteArray& data)>;
    static ImgClient& instance() {
        static ImgClient c;
        return c;
    }

    // 异步 GET;超时/连接失败时 status=0。ctx 只作生命周期哨兵:
    // ctx 已销毁则回调静默丢弃(回调里不许再碰 ctx 的成员)。
    void get(const QString& path, const QList<QPair<QString, QString>>& params,
             int timeoutMs, const QObject* ctx, Callback cb) {
        QUrl u = baseUrl();
        QUrlQuery qu;
        for (const auto& kv : params)
            if (!kv.second.isEmpty()) qu.addQueryItem(kv.first, kv.second);
        u.setQuery(qu);
        QNetworkRequest req(u);
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam.get(req);
        armTimeout(reply, timeoutMs);
        // ctx 挂为连接上下文:ctx 析构 → 连接自动断开,回调不会触碰悬空指针
        auto onDone = [reply, cb = std::move(cb)] {
            const int st = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            reply->deleteLater();
            cb(st, data);
        };
        if (ctx) QObject::connect(reply, &QNetworkReply::finished, ctx, onDone);
        else     QObject::connect(reply, &QNetworkReply::finished, this, onDone);
    }

    void del(const QString& path, int timeoutMs, const QObject* ctx, Callback cb) {
        QNetworkRequest req(baseUrl().resolved(QUrl(path)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        QNetworkReply* reply = m_nam.deleteResource(req);
        armTimeout(reply, timeoutMs);
        auto onDone = [reply, cb = std::move(cb)] {
            const int st = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            reply->deleteLater();
            cb(st, data);
        };
        if (ctx) QObject::connect(reply, &QNetworkReply::finished, ctx, onDone);
        else     QObject::connect(reply, &QNetworkReply::finished, this, onDone);
    }

    void post(const QString& path, const QByteArray& json,
              int timeoutMs, const QObject* ctx, Callback cb) {
        QNetworkRequest req(baseUrl().resolved(QUrl(path)));
        req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                         QNetworkRequest::NoLessSafeRedirectPolicy);
        req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
        QNetworkReply* reply = m_nam.post(req, json);
        armTimeout(reply, timeoutMs);
        auto onDone = [reply, cb = std::move(cb)] {
            const int st = reply->attribute(
                QNetworkRequest::HttpStatusCodeAttribute).toInt();
            const QByteArray data = reply->readAll();
            reply->deleteLater();
            cb(st, data);
        };
        if (ctx) QObject::connect(reply, &QNetworkReply::finished, ctx, onDone);
        else     QObject::connect(reply, &QNetworkReply::finished, this, onDone);
    }

private:
    ImgClient() : QObject(nullptr), m_nam(this) {}
    void armTimeout(QNetworkReply* reply, int timeoutMs) {
        auto* t = new QTimer(reply);
        t->setSingleShot(true);
        QObject::connect(t, &QTimer::timeout, reply, &QNetworkReply::abort);
        t->start(timeoutMs);
    }
    QNetworkAccessManager m_nam;
};

// ── 目录管理/状态(Tier2 #133,字段定案见 docs/imgseek/API_SCHEMA.md)──
inline void statusAsync(const QObject* ctx,
                        std::function<void(int, const QJsonDocument&)> cb) {
    ImgClient::instance().get("/api/status", {}, 4000, ctx,
        [cb = std::move(cb)](int st, const QByteArray& d) { cb(st, QJsonDocument::fromJson(d)); });
}
inline void foldersAsync(const QObject* ctx,
                         std::function<void(int, const QJsonDocument&)> cb) {
    ImgClient::instance().get("/api/folders", {}, 8000, ctx,
        [cb = std::move(cb)](int st, const QByteArray& d) { cb(st, QJsonDocument::fromJson(d)); });
}
inline void postJsonAsync(const QString& path, const QByteArray& json, int timeoutMs,
                          const QObject* ctx,
                          std::function<void(int, const QJsonDocument&)> cb) {
    ImgClient::instance().post(path, json, timeoutMs, ctx,
        [cb = std::move(cb)](int st, const QByteArray& d) { cb(st, QJsonDocument::fromJson(d)); });
}
inline void deleteAsync(const QString& path, int timeoutMs,
                        const QObject* ctx,
                        std::function<void(int, const QJsonDocument&)> cb) {
    ImgClient::instance().del(path, timeoutMs, ctx,
        [cb = std::move(cb)](int st, const QByteArray& d) { cb(st, QJsonDocument::fromJson(d)); });
}

// 服务是否可达:任何 HTTP 应答(含 4xx/5xx)都算进程活着;
// 连接拒绝/超时(status=0)才算离线。ctx 语义同 ImgClient::get。
inline void pingAsync(const QObject* ctx, std::function<void(bool)> cb) {
    ImgClient::instance().get("/api/status", {}, 2000, ctx,
        [cb = std::move(cb)](int status, const QByteArray&) {
            cb(status != 0);
        });
}

// 由本进程拉起的服务 PID(>0 = 是我们拉的,退出时可按设置回收)
inline qint64& servicePid() { static qint64 p = 0; return p; }
inline void killStartedService() {
    const qint64 pid = servicePid();
    if (pid <= 0) return;
    servicePid() = 0;
    QProcess p;
    hideConsoleWindow(p);   // taskkill 是控制台程序,别闪黑窗
    p.start("taskkill", {"/PID", QString::number(pid), "/T", "/F"});
    p.waitForFinished(3000);
}

// 端口上监听进程的 PID(netstat -ano 找 LISTENING 行);找不到返回 0。
// 用于停掉"不是由 Gaze 拉起"的服务实例——端口就是配置里定下的那个,
// 占着它的就是图搜服务本体
inline qint64 pidListeningOnPort(int portNo) {
    QProcess p;
    hideConsoleWindow(p);   // netstat 是控制台程序,不藏会闪黑窗
    p.start("netstat", {"-ano", "-p", "TCP"});
    if (!p.waitForFinished(3000)) return 0;
    const QString out = QString::fromLocal8Bit(p.readAllStandardOutput());
    const QString suffix = QStringLiteral(":%1").arg(portNo);
    for (const QString& raw : out.split(QLatin1Char('\n'))) {
        const QStringList cols = raw.trimmed().split(QRegularExpression("\\s+"));
        if (cols.size() < 5 || cols.at(0) != QLatin1String("TCP")) continue;
        if (cols.at(1).endsWith(suffix) && cols.at(3) == QLatin1String("LISTENING"))
            return cols.at(4).toLongLong();
    }
    return 0;
}

// 引擎开关的"关":优先回收由 Gaze 拉起的实例;否则按端口找监听进程结束。
// 返回空串 = 已结束(或本来就没在跑);非空 = 需要给用户看的失败说明。
inline QString stopService() {
    qint64 pid = servicePid();
    if (pid <= 0) pid = pidListeningOnPort(port());
    if (pid <= 0) return gazeTr("服务未在运行");
    QProcess p;
    hideConsoleWindow(p);
    p.start("taskkill", {"/PID", QString::number(pid), "/T", "/F"});
    p.waitForFinished(3000);
    servicePid() = 0;
    return {};
}

// 拉起服务并轮询到就绪(不做任何开关检查——调用方自己拿主意:
// ensureRunningAsync 查 ImgSearch/autoStart,对话框的"启动引擎"是用户点了才来)。
inline void startServiceAsync(QObject* ctx, std::function<void(QString)> onReady) {
    const QString dir = AppSettings::instance().get(
        "ImgSearch/dir", defaultDir()).toString();
    const QString py = AppSettings::instance().get(
        "ImgSearch/python", QStringLiteral("C:/miniconda3")).toString();
    if (!QFileInfo::exists(dir + "/main.py")) {
        onReady(gazeTr(
            "未找到万象图搜项目:%1/main.py —— 可在 设置 → 以文搜图 改目录").arg(dir));
        return;
    }
    qint64 pid = 0;
    QProcess p;
    hideConsoleWindow(p);   // python 控制台窗口一闪而过同样难看:静默起服务
    if (!p.startDetached(py, {"main.py", "--no-browser",
                              "--port", QString::number(port())},
                         dir, &pid)) {
        onReady(gazeTr(
            "无法启动 Python 解释器:%1 —— 检查 设置 → 以文搜图").arg(py));
        return;
    }
    servicePid() = pid;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 30000;
    // 轮询请求存在在途并发:done 保证 onReady 只回调一次
    auto done = QSharedPointer<bool>::create(false);
    auto* t = new QTimer(ctx);
    QObject::connect(t, &QTimer::timeout, ctx, [ctx, deadline, onReady, t, done]() {
        pingAsync(ctx, [deadline, onReady, t, done](bool alive) {
            if (*done) return;
            if (alive) {
                *done = true;
                t->stop(); t->deleteLater();
                onReady({});
            } else if (QDateTime::currentMSecsSinceEpoch() >= deadline) {
                *done = true;
                t->stop(); t->deleteLater();
                onReady(gazeTr(
                    "服务启动超时(30s)。可手动运行 main.py,或在 设置 → 以文搜图 检查配置"));
            }
        });
    });
    t->start(400);
}

// 搜索前按需拉起:alive 直接用;离线时只有 ImgSearch/autoStart 开着才拉
// (铁律:默认绝不替用户起服务);对话框"启动引擎"走 startServiceAsync。
inline void ensureRunningAsync(QObject* ctx, std::function<void(QString)> onReady) {
    pingAsync(ctx, [ctx, onReady = std::move(onReady)](bool alive) {
        if (alive) { onReady({}); return; }
        if (!AppSettings::instance().get("ImgSearch/autoStart", false).toBool()) {
            onReady(gazeTr(
                "万象图搜服务未运行。可手动运行 main.py,或在 设置 → 以文搜图 打开自动启动"));
            return;
        }
        startServiceAsync(ctx, std::move(onReady));
    });
}

// 搜索(异步)。只传已文档化的参数:q 与 model(cn_clip_b16 / clip_b32);
// 服务端 sort 的取值未核实,排序由调用方在本地对结果重排,不猜参数。
inline void searchAsync(const QString& query, const QString& model,
                        const QObject* ctx,
                        std::function<void(int status, const QJsonDocument&)> cb) {
    QList<QPair<QString, QString>> params = {{"q", query}};
    if (!model.isEmpty()) params.append({"model", model});
    ImgClient::instance().get("/api/search", params, 30000, ctx,
        [cb = std::move(cb)](int status, const QByteArray& data) {
            cb(status, QJsonDocument::fromJson(data));
        });
}

} // namespace ImgSearch
