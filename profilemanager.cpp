#include "profilemanager.h"
#include "chatclient.h"
#include "networkservice.h"
#include "uitexts.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QSettings>
#include <QTimer>

ProfileManager::ProfileManager(NetworkService *networkService, ChatClient *chatClient, QObject *parent)
    : QObject(parent)
    , m_networkService(networkService)
    , m_chatClient(chatClient)
{
}

void ProfileManager::loadCurrentUserProfile(const QString &backendBaseUrl)
{
    if (backendBaseUrl.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/me"));

    m_networkService->getJsonAsync(url, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            return;
        }

        const QJsonObject user = result.body.value(QStringLiteral("user")).toObject();
        const QString nickname = user.value(QStringLiteral("nickname")).toString().trimmed();
        const QString avatarUrl = user.value(QStringLiteral("avatarUrl")).toString().trimmed();

        QSettings settings;
        if (!nickname.isEmpty()) {
            m_currentUserNickname = nickname;
            settings.setValue(QStringLiteral("auth/user_nickname"), nickname);
        }
        m_currentUserAvatarUrl = avatarUrl;
        settings.setValue(QStringLiteral("auth/user_avatar_url"), avatarUrl);
        settings.sync();

        emit profileUpdated();
    });
}

void ProfileManager::updateProfile(const QString &backendBaseUrl, const QString &nickname, const QString &avatarUrl)
{
    if (backendBaseUrl.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/me/profile"));

    m_networkService->postJsonAsync(url,
                  QJsonObject{
                      {QStringLiteral("nickname"), nickname},
                      {QStringLiteral("avatarUrl"), avatarUrl},
                  },
                  [this](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusProfileUpdateFailed, result.message);
                          return;
                      }

                      const QJsonObject user = result.body.value(QStringLiteral("user")).toObject();
                      const QString updatedNickname = user.value(QStringLiteral("nickname")).toString().trimmed();
                      const QString updatedAvatarUrl = user.value(QStringLiteral("avatarUrl")).toString().trimmed();

                      QSettings settings;
                      if (!updatedNickname.isEmpty()) {
                          m_currentUserNickname = updatedNickname;
                          settings.setValue(QStringLiteral("auth/user_nickname"), updatedNickname);
                      }
                      m_currentUserAvatarUrl = updatedAvatarUrl;
                      settings.setValue(QStringLiteral("auth/user_avatar_url"), updatedAvatarUrl);
                      settings.sync();

                      emit networkStatusChanged(UiText::MainWindow::kStatusProfileUpdated, m_currentUserNickname);
                      emit profileUpdated();
                  });
}

void ProfileManager::uploadAvatar(const QString &backendBaseUrl, const QString &filePath)
{
    if (backendBaseUrl.isEmpty() || filePath.isEmpty()) {
        return;
    }

    m_networkService->uploadAvatarAsync(filePath, backendBaseUrl, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusProfileUpdateFailed, result.message);
            return;
        }

        const QJsonObject user = result.body.value(QStringLiteral("user")).toObject();
        const QString uploadedAvatarUrl = user.value(QStringLiteral("avatarUrl")).toString().trimmed();

        updateProfile(m_networkService->backendBaseUrl(), m_currentUserNickname, uploadedAvatarUrl);
    });
}

void ProfileManager::loadFriends(const QString &backendBaseUrl)
{
    if (backendBaseUrl.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/friends"));

    m_networkService->getJsonAsync(url, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLoadFriendsFailed, result.message);
            return;
        }

        emit friendsLoaded(result.body);
    });
}

void ProfileManager::sendFriendRequest(const QString &backendBaseUrl, const QString &peerEmail)
{
    if (backendBaseUrl.isEmpty() || peerEmail.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/friends/request"));

    m_networkService->postJsonAsync(url,
                  QJsonObject{{QStringLiteral("peerEmail"), peerEmail}},
                  [this](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusSendFriendRequestFailed, result.message);
                          return;
                      }

                      if (result.body.value(QStringLiteral("alreadyFriends")).toBool()) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusAlreadyFriends);
                          return;
                      }

                      if (result.body.value(QStringLiteral("autoAccepted")).toBool()) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusFriendsAutoAccepted);
                          return;
                      }

                      emit networkStatusChanged(UiText::MainWindow::kStatusFriendRequestSent);
                  });
}

void ProfileManager::loadFriendRequests(const QString &backendBaseUrl)
{
    if (backendBaseUrl.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/friend-requests"));

    m_networkService->getJsonAsync(url, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLoadFriendRequestsFailed, result.message);
            return;
        }

        emit friendRequestsLoaded(result.body);
    });
}

void ProfileManager::acceptFriendRequest(const QString &backendBaseUrl, qint64 requestId)
{
    if (backendBaseUrl.isEmpty() || requestId <= 0) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/friend-requests/%1/accept").arg(requestId));

    m_networkService->postJsonAsync(url, QJsonObject{}, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusHandleFriendRequestFailed, result.message);
            return;
        }

        emit networkStatusChanged(UiText::MainWindow::kStatusFriendRequestAccepted);
    });
}

void ProfileManager::rejectFriendRequest(const QString &backendBaseUrl, qint64 requestId)
{
    if (backendBaseUrl.isEmpty() || requestId <= 0) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/friend-requests/%1/reject").arg(requestId));

    m_networkService->postJsonAsync(url, QJsonObject{}, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusHandleFriendRequestFailed, result.message);
            return;
        }

        emit networkStatusChanged(UiText::MainWindow::kStatusFriendRequestRejected);
    });
}

void ProfileManager::loadBlacklist(const QString &backendBaseUrl)
{
    if (backendBaseUrl.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/blacklist"));

    m_networkService->getJsonAsync(url, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLoadBlacklistFailed, result.message);
            return;
        }

        emit blacklistLoaded(result.body);
    });
}

void ProfileManager::blockUser(const QString &backendBaseUrl, const QString &peerEmail)
{
    if (backendBaseUrl.isEmpty() || peerEmail.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/blacklist"));

    m_networkService->postJsonAsync(url,
                  QJsonObject{{QStringLiteral("peerEmail"), peerEmail}},
                  [this](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusBlockFailed, result.message);
                          return;
                      }

                      emit networkStatusChanged(UiText::MainWindow::kStatusBlockedUser);
                  });
}

void ProfileManager::unblockUser(const QString &backendBaseUrl, qint64 userId)
{
    if (backendBaseUrl.isEmpty() || userId <= 0) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/blacklist/%1/remove").arg(userId));

    m_networkService->postJsonAsync(url, QJsonObject{}, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusUnblockFailed, result.message);
            return;
        }

        emit networkStatusChanged(UiText::MainWindow::kStatusUnblockedUser);
    });
}

void ProfileManager::updateTypingState(const QString &conversationId, bool isTyping)
{
    if (conversationId.isEmpty()) {
        return;
    }

    if (isTyping) {
        if (m_typingTimers.contains(conversationId)) {
            m_typingTimers[conversationId]->stop();
        } else {
            auto *timer = new QTimer(this);
            timer->setSingleShot(true);
            connect(timer, &QTimer::timeout, this, [this, conversationId]() {
                m_chatClient->setTypingState(conversationId, false);
                m_typingTimers.remove(conversationId);
            });
            m_typingTimers.insert(conversationId, timer);
        }
        m_chatClient->setTypingState(conversationId, true);
        m_typingTimers[conversationId]->start(1800);
    } else {
        if (m_typingTimers.contains(conversationId)) {
            m_typingTimers[conversationId]->stop();
            m_typingTimers.remove(conversationId);
        }
        m_chatClient->setTypingState(conversationId, false);
    }
}

void ProfileManager::clearTypingState(const QString &conversationId)
{
    if (conversationId.isEmpty()) {
        return;
    }

    if (m_typingTimers.contains(conversationId)) {
        m_typingTimers[conversationId]->stop();
        m_typingTimers.remove(conversationId);
    }
}

QString ProfileManager::currentUserEmail() const
{
    return m_currentUserEmail;
}

QString ProfileManager::currentUserNickname() const
{
    return m_currentUserNickname;
}

QString ProfileManager::currentUserAvatarUrl() const
{
    return m_currentUserAvatarUrl;
}

void ProfileManager::setCurrentUserEmail(const QString &email)
{
    m_currentUserEmail = email.trimmed();
}

void ProfileManager::setCurrentUserNickname(const QString &nickname)
{
    m_currentUserNickname = nickname.trimmed();
}

void ProfileManager::setCurrentUserAvatarUrl(const QString &avatarUrl)
{
    m_currentUserAvatarUrl = avatarUrl.trimmed();
}

void ProfileManager::scheduleTypingStateUpdate(const QString &conversationId, bool shouldBeTyping)
{
    if (conversationId.isEmpty()) {
        return;
    }

    if (shouldBeTyping) {
        if (!m_isTypingActive || m_typingConversationId != conversationId) {
            sendTypingState(conversationId, true);
        }
        if (m_typingStopTimer) {
            m_typingStopTimer->start(1800);
        }
        return;
    }

    if (m_isTypingActive && m_typingConversationId == conversationId) {
        sendTypingState(conversationId, false);
    }
}

void ProfileManager::sendTypingState(const QString &conversationId, bool isTyping)
{
    if (!m_chatClient) {
        return;
    }

    if (conversationId.isEmpty()) {
        return;
    }

    if (isTyping) {
        m_typingConversationId = conversationId;
        m_isTypingActive = true;
    } else {
        if (!m_isTypingActive && m_typingConversationId.isEmpty()) {
            return;
        }
        if (!m_typingConversationId.isEmpty() && m_typingConversationId != conversationId) {
            m_chatClient->setTypingState(m_typingConversationId, false);
        } else {
            m_chatClient->setTypingState(conversationId, false);
        }
        m_typingConversationId.clear();
        m_isTypingActive = false;
        if (m_typingStopTimer) {
            m_typingStopTimer->stop();
        }
        return;
    }

    m_chatClient->setTypingState(conversationId, true);
}

void ProfileManager::clearTypingUsersForConversation(const QString &conversationId)
{
    const QString normalizedId = conversationId.trimmed();
    if (normalizedId.isEmpty()) {
        return;
    }

    m_typingUsersByConversationId.remove(normalizedId);
    emit typingUsersUpdated(normalizedId, QHash<QString, QString>());
}

void ProfileManager::updateTypingStatusLabel()
{
    const QString conversationId = m_typingConversationId.trimmed();
    if (conversationId.isEmpty()) {
        emit typingStatusLabelUpdated(QString(), false);
        return;
    }

    const QHash<QString, QString> typingUsers = m_typingUsersByConversationId.value(conversationId);
    if (typingUsers.isEmpty()) {
        emit typingStatusLabelUpdated(QString(), false);
        return;
    }

    QStringList names;
    for (auto it = typingUsers.constBegin(); it != typingUsers.constEnd(); ++it) {
        names.append(it.value().isEmpty() ? it.key() : it.value());
    }
    names.removeAll(QString());
    if (names.isEmpty()) {
        emit typingStatusLabelUpdated(QString(), false);
        return;
    }

    names.sort();
    const QString text = names.size() == 1
        ? QStringLiteral("%1 正在输入...").arg(names.first())
        : QStringLiteral("%1 等人正在输入...").arg(names.first());
    emit typingStatusLabelUpdated(text, true);
}
