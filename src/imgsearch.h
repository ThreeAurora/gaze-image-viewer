#pragma once
// ═══════════════════════════════════════════════════════════
// 万象图搜(imgseek)客户端助手 —— 进程外集成
// imgseek 是独立 Python 服务(FastAPI,默认 127.0.0.1:8747):
//   GET /api/search?q=<关键词>  → {"total":N,"results":[{id,path,filename,sources:[{type,score}]}]}
//   GET /api/thumb/<image_id>   → WebP 缩略图
// Gaze 侧只发 HTTP,不导入任何推理代码;服务离线时自动拉起
//   (python main.py --no-browser,cwd=项目目录),失败给出明确提示。
// ═══════════════════════════════════════════════════════════

#include <QString>
#include <QUrl>
#include <QUrlQuery>
#include <QNetworkAccessManager>
#include <QNetworkRequest>
#include <QNetworkReply>
#include <QEventLoop>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QCoreApplication>
#include <QFileInfo>
#include <QDir>
#include <QThread>
#include <QDateTime>
#include "settings.h"

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

// 同步 GET(短超时;GUI 线程用 QEventLoop 等待,仅限低频调用)
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

// 服务是否在线
inline bool ping(int timeoutMs = 500) {
    QUrl u = baseUrl();
    QUrlQuery qu; qu.addQueryItem("q", "ping"); u.setQuery(qu);
    int status = 0;
    httpGet(u, timeoutMs, &status);
    return status == 200;
}

// 服务离线时自动拉起(python main.py --no-browser),最多等 timeoutMs 毫秒
inline QString ensureRunning(int timeoutMs = 25000) {
    if (ping()) return {};
    const QString dir = AppSettings::instance().get("ImgSearch/dir", defaultDir()).toString();
    const QString py = AppSettings::instance().get(
        "ImgSearch/python", QStringLiteral("C:/miniconda3")).toString();
    if (!QFileInfo::exists(dir + "/main.py"))
        return QString::fromUtf8("未找到万象图搜项目:%1/main.py").arg(dir);
    QProcess::startDetached(py, {"main.py", "--no-browser", "--port", QString::number(port())}, dir);
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    while (QDateTime::currentMSecsSinceEpoch() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        if (ping(300)) return {};
        QThread::msleep(120);
    }
    return QString::fromUtf8("服务启动超时(%1s)。请手动运行:%2/main.py")
        .arg(timeoutMs / 1000).arg(dir);
}

// 搜索:返回解析后的 JSON 文档;失败时 errorInfo 非空
inline QJsonDocument search(const QString& query, QString* errorInfo = nullptr,
                            int timeoutMs = 30000) {
    QUrl u = baseUrl();
    QUrlQuery qu;
    qu.addQueryItem("q", query);
    qu.addQueryItem("sort", "relevance");
    u.setQuery(qu);
    QString err;
    QJsonDocument doc;
    int status = 0;
    const QByteArray data = httpGet(u, timeoutMs, &status);
    if (status != 200) {
        err = QString::fromUtf8("搜索请求失败(HTTP %1)").arg(status);
    } else {
        doc = QJsonDocument::fromJson(data);
        if (!doc.isObject()) err = QString::fromUtf8("响应不是有效 JSON");
    }
    if (errorInfo) *errorInfo = err;
    return doc;
}

// 缩略图字节(WebP;id 为 imgseek 库内 image id)
inline QByteArray thumbBytes(int imageId, int timeoutMs = 5000) {
    return httpGet(baseUrl().toString() + QString("/api/thumb/%1").arg(imageId),
                   timeoutMs);
}

} // namespace ImgSearch
