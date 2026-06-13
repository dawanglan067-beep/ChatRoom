#pragma once

#include <functional>
#include <QJsonObject>
#include <QObject>
#include <QUrl>

class QHttpMultiPart;
class QNetworkAccessManager;
class QNetworkReply;

class NetworkService : public QObject
{
    Q_OBJECT

public:
    struct HttpResult
    {
        bool ok = false;
        int statusCode = 0;
        QJsonObject body;
        QString message;
    };

    using HttpCallback = std::function<void(const HttpResult &)>;

    explicit NetworkService(QObject *parent = nullptr);

    void setAuthToken(const QString &token);
    void setBackendBaseUrl(const QString &baseUrl);
    QString authToken() const;
    QString backendBaseUrl() const;

    void getJsonAsync(const QUrl &url, HttpCallback callback);
    void postJsonAsync(const QUrl &url, const QJsonObject &payload, HttpCallback callback);
    void postMultipartAsync(const QUrl &url, QHttpMultiPart *multipart, HttpCallback callback);
    void uploadAvatarAsync(const QString &filePath, const QString &backendBaseUrl, HttpCallback callback);

    QNetworkAccessManager *networkManager() const;

    static HttpResult parseReplyResult(QNetworkReply *reply);
    static void setupReplyTimeout(QNetworkReply *reply, int timeoutMs);

signals:
    void networkStatusChanged(const QString &status, const QString &detail = QString());

private:
    QNetworkAccessManager *m_networkManager = nullptr;
    QString m_authToken;
    QString m_backendBaseUrl;
};
