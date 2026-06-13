#include "networkservice.h"
#include "uitexts.h"

#include <QFile>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QMimeDatabase>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace
{
constexpr int kDefaultTimeoutMs = 10000;
constexpr int kUploadTimeoutMs = 30000;
constexpr int kMaxUploadBytes = 8 * 1024 * 1024;

void setupAuthHeader(QNetworkRequest &request, const QString &authToken)
{
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(authToken).toUtf8());
    }
}

}

NetworkService::HttpResult NetworkService::parseReplyResult(QNetworkReply *reply)
{
    HttpResult result;
    const bool timedOut = reply->property("chatroomTimedOut").toBool();

    const QByteArray bodyBytes = reply->readAll();
    result.statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parseError);
    if (parseError.error == QJsonParseError::NoError && document.isObject()) {
        result.body = document.object();
    }

    if (timedOut) {
        result.message = UiText::MainWindow::kBackendTimeoutShort;
    } else if (reply->error() != QNetworkReply::NoError) {
        result.message = result.body.value(QStringLiteral("message")).toString().trimmed();
        if (result.message.isEmpty()) {
            result.message = reply->errorString();
        }
    } else if (result.statusCode < 200 || result.statusCode >= 300) {
        result.message = result.body.value(QStringLiteral("message")).toString().trimmed();
        if (result.message.isEmpty()) {
            result.message = UiText::MainWindow::kHttpErrorPattern.arg(result.statusCode);
        }
    } else {
        result.ok = true;
    }

    return result;
}

void NetworkService::setupReplyTimeout(QNetworkReply *reply, int timeoutMs)
{
    auto *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    QObject::connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("chatroomTimedOut", true);
        reply->abort();
    });
    timeoutTimer->start(timeoutMs);
}

NetworkService::NetworkService(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
{
}

void NetworkService::setAuthToken(const QString &token)
{
    m_authToken = token.trimmed();
}

void NetworkService::setBackendBaseUrl(const QString &baseUrl)
{
    m_backendBaseUrl = baseUrl.trimmed();
}

QString NetworkService::authToken() const
{
    return m_authToken;
}

QString NetworkService::backendBaseUrl() const
{
    return m_backendBaseUrl;
}

QNetworkAccessManager *NetworkService::networkManager() const
{
    return m_networkManager;
}

void NetworkService::getJsonAsync(const QUrl &url, HttpCallback callback)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    setupAuthHeader(request, m_authToken);

    QNetworkReply *reply = m_networkManager->get(request);
    setupReplyTimeout(reply, kDefaultTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
        HttpResult result = parseReplyResult(reply);
        reply->deleteLater();

        if (callback) {
            callback(result);
        }
    });
}

void NetworkService::postJsonAsync(const QUrl &url, const QJsonObject &payload, HttpCallback callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");
    setupAuthHeader(request, m_authToken);

    const QByteArray requestBody = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    QNetworkReply *reply = m_networkManager->post(request, requestBody);
    setupReplyTimeout(reply, kDefaultTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
        HttpResult result = parseReplyResult(reply);
        reply->deleteLater();

        if (callback) {
            callback(result);
        }
    });
}

void NetworkService::postMultipartAsync(const QUrl &url, QHttpMultiPart *multipart, HttpCallback callback)
{
    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    setupAuthHeader(request, m_authToken);

    QNetworkReply *reply = m_networkManager->post(request, multipart);
    multipart->setParent(reply);
    setupReplyTimeout(reply, kUploadTimeoutMs);

    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
        HttpResult result = parseReplyResult(reply);
        reply->deleteLater();

        if (callback) {
            callback(result);
        }
    });
}

void NetworkService::uploadAvatarAsync(const QString &filePath, const QString &backendBaseUrl, HttpCallback callback)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        HttpResult result;
        result.message = UiText::NetworkService::kCannotOpenAvatar;
        if (callback) {
            callback(result);
        }
        return;
    }

    const QByteArray bytes = file.readAll();
    if (bytes.isEmpty()) {
        HttpResult result;
        result.message = UiText::NetworkService::kAvatarEmpty;
        if (callback) {
            callback(result);
        }
        return;
    }
    if (bytes.size() > kMaxUploadBytes) {
        HttpResult result;
        result.message = UiText::NetworkService::kAvatarTooLarge;
        if (callback) {
            callback(result);
        }
        return;
    }

    const QFileInfo info(file);
    const QString fileName = info.fileName().trimmed().isEmpty()
                                 ? QStringLiteral("avatar.jpg")
                                 : info.fileName().trimmed();
    QMimeDatabase mimeDatabase;
    QString mimeType = mimeDatabase.mimeTypeForFile(info).name().trimmed();
    if (mimeType.isEmpty()) {
        mimeType = QStringLiteral("application/octet-stream");
    }
    if (!mimeType.startsWith(QStringLiteral("image/"))) {
        HttpResult result;
        result.message = UiText::NetworkService::kSelectImageFile;
        if (callback) {
            callback(result);
        }
        return;
    }

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addTextPart = [multipart](const QString &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multipart->append(part);
    };

    addTextPart(QStringLiteral("fileName"), fileName);
    addTextPart(QStringLiteral("mimeType"), mimeType);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(fileName));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    filePart.setBody(bytes);
    multipart->append(filePart);

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/me/avatar"));

    emit networkStatusChanged(UiText::NetworkService::kUploadingAvatar, fileName);
    postMultipartAsync(url, multipart, std::move(callback));
}
