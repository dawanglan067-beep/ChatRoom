#pragma once

#include <QObject>
#include <QJsonObject>
#include <QJsonArray>
#include <QString>
#include <QList>
#include <QSet>

class ChatStore;
class NetworkService;
class Conversation;
class QModelIndex;

class ConversationManager : public QObject
{
    Q_OBJECT

public:
    explicit ConversationManager(ChatStore *chatStore, NetworkService *networkService, QObject *parent = nullptr);

    // Business logic methods
    void loadConversations(const QString &backendBaseUrl, const QString &loggedInUserEmail);
    void loadConversationMembers(const QString &backendBaseUrl, int index);
    void createDirectConversation(const QString &backendBaseUrl, const QString &peerEmail);
    void createGroupConversation(const QString &backendBaseUrl, const QString &groupName, const QJsonArray &memberEmails);
    void inviteMembers(const QString &backendBaseUrl, const QString &conversationId, const QJsonArray &memberEmails);
    void removeMember(const QString &backendBaseUrl, const QString &conversationId, const QString &memberEmail);
    void leaveConversation(const QString &backendBaseUrl, const QString &conversationId);
    void deleteCurrentConversation();
    void togglePinCurrentConversation();
    void toggleMuteCurrentConversation();
    void markConversationRead(const QString &backendBaseUrl, const QString &conversationId, qint64 messageId);

    // Query methods
    QString currentRoomId() const;
    QString currentRoomName() const;
    bool currentConversationIsGroup() const;
    bool selectConversationById(const QString &conversationId);
    bool isConversationPinned(const QString &conversationId) const;
    bool isConversationMuted(const QString &conversationId) const;
    int currentConversationIndex() const;
    const Conversation* currentConversation() const;

signals:
    void conversationsLoaded();
    void conversationMembersLoaded(const QString &conversationId);
    void conversationCreated(const QString &conversationId);
    void conversationError(const QString &errorMessage);
    void conversationSelected(int index);
    void conversationHeaderRefreshed();
    void networkStatusChanged(const QString &status, const QString &detail = QString());

private:
    ChatStore *m_chatStore = nullptr;
    NetworkService *m_networkService = nullptr;
    QSet<QString> m_pinnedConversationIds;
    QSet<QString> m_mutedConversationIds;
};
