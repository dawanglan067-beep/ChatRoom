#pragma once

#include <functional>
#include <QObject>
#include <QJsonObject>
#include <QString>

class QNetworkAccessManager;
class QUrl;

struct SendCodeResult
{
    bool ok = false;
    QString message;
};

struct VerifyAuthResult
{
    bool ok = false;
    bool newlyRegistered = false;
    QString message;
};

class AuthService : public QObject
{
    Q_OBJECT

public:
    using SendCodeCallback = std::function<void(const SendCodeResult &)>;
    using VerifyAuthCallback = std::function<void(const VerifyAuthResult &)>;

    explicit AuthService(QObject *parent = nullptr);

    bool saveBackendBaseUrl(const QString &baseUrl, QString *errorMessage = nullptr);
    QString backendBaseUrl() const;

    void sendVerificationCodeAsync(const QString &baseUrl, const QString &email, SendCodeCallback callback);
    void verifyCodeAndAuthenticateAsync(const QString &baseUrl, const QString &email, const QString &code,
                                        VerifyAuthCallback callback);

    QString currentUserEmail() const;
    QString currentUserNickname() const;
    QString authToken() const;

private:
    struct HttpResult
    {
        bool ok = false;
        int statusCode = 0;
        QJsonObject body;
        QString message;
    };

    bool validateEmail(const QString &email, QString *errorMessage) const;
    bool validateVerificationCode(const QString &code, QString *errorMessage) const;
    QUrl normalizedBaseUrl(const QString &baseUrl, QString *errorMessage = nullptr) const;
    void postJsonAsync(const QUrl &url, const QJsonObject &payload,
                       std::function<void(const HttpResult &)> callback);
    void loadPersistedSession();
    void persistAuthenticatedUser(const QString &baseUrl, const QJsonObject &payload);

    QNetworkAccessManager *m_networkAccessManager = nullptr;
    QString m_backendBaseUrl;
    QString m_currentUserEmail;
    QString m_currentUserNickname;
    QString m_authToken;
};
