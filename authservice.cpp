#include "authservice.h"
#include "chatutils.h"
#include "uitexts.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSettings>
#include <QTimer>
#include <QUrl>

namespace
{
QString responseMessage(const QJsonObject &body, const QString &fallback)
{
    const QString detail = body.value(QStringLiteral("message")).toString().trimmed();
    return detail.isEmpty() ? fallback : detail;
}
}

AuthService::AuthService(QObject *parent)
    : QObject(parent)
    , m_networkAccessManager(new QNetworkAccessManager(this))
{
    loadPersistedSession();
}

bool AuthService::saveBackendBaseUrl(const QString &baseUrl, QString *errorMessage)
{
    const QUrl normalizedUrl = normalizedBaseUrl(baseUrl, errorMessage);
    if (!normalizedUrl.isValid()) {
        return false;
    }

    m_backendBaseUrl = normalizedUrl.toString(QUrl::RemoveQuery | QUrl::RemoveFragment);

    QSettings settings;
    settings.setValue(QStringLiteral("auth/backend_base_url"), m_backendBaseUrl);
    settings.sync();
    return true;
}

QString AuthService::backendBaseUrl() const
{
    return m_backendBaseUrl;
}

void AuthService::sendVerificationCodeAsync(const QString &baseUrl, const QString &email,
                                            SendCodeCallback callback)
{
    QString errorMessage;
    if (!validateEmail(email, &errorMessage)) {
        if (callback) {
            callback({ false, errorMessage });
        }
        return;
    }

    const QUrl normalizedUrl = normalizedBaseUrl(baseUrl, &errorMessage);
    if (!normalizedUrl.isValid()) {
        if (callback) {
            callback({ false, errorMessage });
        }
        return;
    }

    saveBackendBaseUrl(normalizedUrl.toString());

    QUrl endpoint = normalizedUrl;
    endpoint.setPath(QStringLiteral("/api/auth/request-code"));

    QJsonObject payload;
    payload.insert(QStringLiteral("email"), email.trimmed().toLower());

    postJsonAsync(endpoint, payload, [callback = std::move(callback)](const HttpResult &result) mutable {
        SendCodeResult sendCodeResult;
        sendCodeResult.ok = result.ok;
        sendCodeResult.message = result.ok
            ? responseMessage(result.body, UiText::AuthService::kCodeSentFallback)
            : result.message;
        if (callback) {
            callback(sendCodeResult);
        }
    });
}

void AuthService::verifyCodeAndAuthenticateAsync(const QString &baseUrl, const QString &email,
                                                 const QString &code, VerifyAuthCallback callback)
{
    QString errorMessage;
    if (!validateEmail(email, &errorMessage)) {
        if (callback) {
            callback({ false, false, errorMessage });
        }
        return;
    }
    if (!validateVerificationCode(code, &errorMessage)) {
        if (callback) {
            callback({ false, false, errorMessage });
        }
        return;
    }

    const QUrl normalizedUrl = normalizedBaseUrl(baseUrl, &errorMessage);
    if (!normalizedUrl.isValid()) {
        if (callback) {
            callback({ false, false, errorMessage });
        }
        return;
    }

    saveBackendBaseUrl(normalizedUrl.toString());

    QUrl endpoint = normalizedUrl;
    endpoint.setPath(QStringLiteral("/api/auth/verify-code"));

    QJsonObject payload;
    payload.insert(QStringLiteral("email"), email.trimmed().toLower());
    payload.insert(QStringLiteral("code"), code.trimmed());

    postJsonAsync(endpoint, payload,
                  [this, normalizedUrl, callback = std::move(callback)](const HttpResult &result) mutable {
                      if (!result.ok) {
                          if (callback) {
                              callback({ false, false, result.message });
                          }
                          return;
                      }

                      persistAuthenticatedUser(normalizedUrl.toString(), result.body);

                      VerifyAuthResult verifyResult;
                      verifyResult.ok = true;
                      verifyResult.newlyRegistered = result.body.value(QStringLiteral("isNewUser")).toBool();
                      verifyResult.message = responseMessage(result.body, UiText::AuthService::kSignInSuccessFallback);
                      if (callback) {
                          callback(verifyResult);
                      }
                  });
}

QString AuthService::currentUserEmail() const
{
    return m_currentUserEmail;
}

QString AuthService::currentUserNickname() const
{
    return m_currentUserNickname;
}

QString AuthService::authToken() const
{
    return m_authToken;
}

bool AuthService::validateEmail(const QString &email, QString *errorMessage) const
{
    static const QRegularExpression emailPattern(
        QStringLiteral(R"(^[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}$)"));

    if (!emailPattern.match(email.trimmed()).hasMatch()) {
        if (errorMessage) {
            *errorMessage = UiText::AuthService::kInvalidEmail;
        }
        return false;
    }

    return true;
}

bool AuthService::validateVerificationCode(const QString &code, QString *errorMessage) const
{
    static const QRegularExpression codePattern(QStringLiteral(R"(^\d{6}$)"));
    if (!codePattern.match(code.trimmed()).hasMatch()) {
        if (errorMessage) {
            *errorMessage = UiText::AuthService::kInvalidCode;
        }
        return false;
    }

    return true;
}

QUrl AuthService::normalizedBaseUrl(const QString &baseUrl, QString *errorMessage) const
{
    QUrl url = QUrl::fromUserInput(baseUrl.trimmed());
    if (!url.isValid() || url.scheme().isEmpty() || url.host().isEmpty()) {
        if (errorMessage) {
            *errorMessage = UiText::AuthService::kInvalidBackendUrl;
        }
        return {};
    }

    if (url.path().isEmpty()) {
        url.setPath(QStringLiteral("/"));
    }

    return url.adjusted(QUrl::StripTrailingSlash);
}

void AuthService::postJsonAsync(const QUrl &url, const QJsonObject &payload,
                                std::function<void(const HttpResult &)> callback)
{
    QNetworkRequest request(url);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    request.setRawHeader("Accept", "application/json");

    QNetworkReply *reply =
        m_networkAccessManager->post(request, QJsonDocument(payload).toJson(QJsonDocument::Compact));

    auto *timeoutTimer = new QTimer(reply);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, reply, [reply]() {
        reply->setProperty("chatroomTimedOut", true);
        reply->abort();
    });
    timeoutTimer->start(10000);

    connect(reply, &QNetworkReply::finished, this, [reply, callback = std::move(callback)]() mutable {
        HttpResult result;
        const bool timedOut = reply->property("chatroomTimedOut").toBool();

        const QByteArray bodyBytes = reply->readAll();
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        result.statusCode = statusCode;

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(bodyBytes, &parseError);
        if (parseError.error == QJsonParseError::NoError && document.isObject()) {
            result.body = document.object();
        }

        if (timedOut) {
            result.message = UiText::AuthService::kBackendTimeout;
        } else if (reply->error() != QNetworkReply::NoError) {
            result.message = responseMessage(
                result.body,
                UiText::AuthService::kRequestFailedPattern.arg(reply->errorString()));
        } else if (statusCode < 200 || statusCode >= 300) {
            result.message = responseMessage(
                result.body,
                UiText::AuthService::kBackendHttpErrorPattern.arg(statusCode));
        } else {
            result.ok = true;
        }

        reply->deleteLater();

        if (callback) {
            callback(result);
        }
    });
}

void AuthService::loadPersistedSession()
{
    QSettings settings;
    m_backendBaseUrl = settings.value(QStringLiteral("auth/backend_base_url"),
                                      QStringLiteral("http://127.0.0.1:3000"))
                           .toString()
                           .trimmed();
    m_currentUserEmail = settings.value(QStringLiteral("auth/user_email")).toString().trimmed();
    m_currentUserNickname = settings.value(QStringLiteral("auth/user_nickname")).toString().trimmed();
    m_authToken = ChatUtils::SecureStorage::secureGetValue(QStringLiteral("auth/token"));
}

void AuthService::persistAuthenticatedUser(const QString &baseUrl, const QJsonObject &payload)
{
    const QJsonObject user = payload.value(QStringLiteral("user")).toObject();

    m_backendBaseUrl = baseUrl;
    m_currentUserEmail = user.value(QStringLiteral("email")).toString().trimmed();
    m_currentUserNickname = user.value(QStringLiteral("nickname")).toString().trimmed();
    m_authToken = payload.value(QStringLiteral("token")).toString().trimmed();

    QSettings settings;
    settings.setValue(QStringLiteral("auth/backend_base_url"), m_backendBaseUrl);
    settings.setValue(QStringLiteral("auth/token_expires_at"),
                      payload.value(QStringLiteral("expiresAt")).toString());
    settings.setValue(QStringLiteral("auth/user_id"),
                      user.value(QStringLiteral("id")).toVariant());
    settings.setValue(QStringLiteral("auth/user_email"), m_currentUserEmail);
    settings.setValue(QStringLiteral("auth/user_nickname"), m_currentUserNickname);
    settings.setValue(QStringLiteral("auth/user_avatar_url"),
                      user.value(QStringLiteral("avatarUrl")).toString().trimmed());
    settings.sync();

    ChatUtils::SecureStorage::secureSetValue(QStringLiteral("auth/token"), m_authToken);
}
