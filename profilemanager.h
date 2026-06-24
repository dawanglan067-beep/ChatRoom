#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QHash>
#include <QTimer>

class NetworkService;
class ChatClient;

class ProfileManager : public QObject
{
    Q_OBJECT

public:
    explicit ProfileManager(NetworkService *networkService, ChatClient *chatClient, QObject *parent = nullptr);

    void loadCurrentUserProfile(const QString &backendBaseUrl);
    void updateProfile(const QString &backendBaseUrl, const QString &nickname, const QString &avatarUrl);
    void uploadAvatar(const QString &backendBaseUrl, const QString &filePath);

    void loadFriends(const QString &backendBaseUrl);
    void sendFriendRequest(const QString &backendBaseUrl, const QString &peerEmail);
    void loadFriendRequests(const QString &backendBaseUrl);
    void acceptFriendRequest(const QString &backendBaseUrl, qint64 requestId);
    void rejectFriendRequest(const QString &backendBaseUrl, qint64 requestId);

    void loadBlacklist(const QString &backendBaseUrl);
    void blockUser(const QString &backendBaseUrl, const QString &peerEmail);
    void unblockUser(const QString &backendBaseUrl, qint64 userId);

    void updateTypingState(const QString &conversationId, bool isTyping);
    void clearTypingState(const QString &conversationId);
    void scheduleTypingStateUpdate(const QString &conversationId, bool shouldBeTyping);
    void sendTypingState(const QString &conversationId, bool isTyping);
    void clearTypingUsersForConversation(const QString &conversationId);

    QString currentUserEmail() const;
    QString currentUserNickname() const;
    QString currentUserAvatarUrl() const;
    void setCurrentUserEmail(const QString &email);
    void setCurrentUserNickname(const QString &nickname);
    void setCurrentUserAvatarUrl(const QString &avatarUrl);

signals:
    void profileUpdated();
    void friendsLoaded(const QJsonObject &friends);
    void friendRequestsLoaded(const QJsonObject &requests);
    void blacklistLoaded(const QJsonObject &blockedUsers);
    void networkStatusChanged(const QString &status, const QString &detail = QString());
    void typingUsersUpdated(const QString &conversationId, const QHash<QString, QString> &users);

private:
    NetworkService *m_networkService = nullptr;
    ChatClient *m_chatClient = nullptr;

    QString m_currentUserEmail;
    QString m_currentUserNickname;
    QString m_currentUserAvatarUrl;

    QHash<QString, QTimer *> m_typingTimers;
    QHash<QString, QHash<QString, QString>> m_typingUsersByConversationId;
    QString m_typingConversationId;
    bool m_isTypingActive = false;
    QTimer *m_typingStopTimer = nullptr;
};
