#pragma once

#include <QObject>
#include <QJsonObject>
#include <QString>
#include <QHash>
#include <QTimer>

class ChatStore;
class ChatClient;
class NetworkService;
class Message;
class QNetworkAccessManager;
class QWidget;

class MessageHandler : public QObject
{
    Q_OBJECT

public:
    explicit MessageHandler(ChatStore *chatStore, ChatClient *chatClient, NetworkService *networkService, QObject *parent = nullptr);
    ~MessageHandler() override;

    void setLoggedInEmail(const QString &email);

    void loadMessages(const QString &backendBaseUrl, int index, qint64 beforeId = 0, bool prepend = false);
    void sendMessage(const QString &text, const QString &conversationId);
    void sendMediaFile(const QString &backendBaseUrl, const QString &filePath, const QString &conversationId, QWidget *parent = nullptr);
    void recallMessage(const QString &conversationId, qint64 messageId);
    void markMessageRead(const QString &backendBaseUrl, const QString &conversationId, qint64 messageId);

    void handleRawMessageReceived(const QString &messageText);
    void handleJsonMessageReceived(const QJsonObject &payload);

    void saveDraft(const QString &conversationId, const QString &draftText);
    void restoreDraft(const QString &conversationId);
    void loadDraftsFromSettings(const QString &loggedInEmail = QString());
    void persistDraftsToSettings() const;

    void toggleFavoriteRich(const QString &conversationId, const QString &conversationName,
                            qint64 serverMessageId, const QString &content,
                            const QString &senderId, qint64 timestamp);
    bool removeFavorite(const QString &conversationId, qint64 serverMessageId);
    void loadFavoritesFromSettings();
    void persistFavoritesToSettings() const;
    bool isFavoriteMessage(const QString &conversationId, qint64 serverMessageId) const;
    QList<QJsonObject> allFavoriteMessages() const;
    static QString favoriteKey(const QString &conversationId, qint64 serverMessageId);

    void addPendingConversationMapping(const QString &clientMessageId, const QString &conversationId);
    void removePendingConversationMapping(const QString &clientMessageId);
    QStringList allPendingClientMessageIds() const;
    QString pendingConversationId(const QString &clientMessageId) const;

    void updateLastReadAck(const QString &conversationId, qint64 serverMessageId);
    qint64 lastReadAck(const QString &conversationId) const;

    QHash<QString, QString> typingUsersForConversation(const QString &conversationId) const;
    void clearTypingUsers();
    void clearTypingUsersForConversation(const QString &conversationId);

    void editMessage(const QString &conversationId, qint64 messageId, const QString &newContent);
    void focusMessageByServerIdInConversation(const QString &conversationId, qint64 serverMessageId);
    void refreshFavoriteHighlights();
    void appendSystemMessage(const QString &text);
    void joinCurrentRoomIfConnected();
    void restartPendingMessageTimeout(const QString &conversationId, const QString &clientMessageId);
    void clearPendingMessageTimeout(const QString &clientMessageId);
    void resumeQueuedMessagesAfterReconnect();

    // Message processing
    Message processMessageFromBackend(const QJsonObject &object, const QString &loggedInEmail);
    void processMessageCreated(const QJsonObject &payload, const QString &loggedInEmail);
    void processMessageRecalled(const QJsonObject &payload, const QString &loggedInEmail);
    void processConversationRead(const QJsonObject &payload, const QString &loggedInEmail);
    void processTypingState(const QJsonObject &payload, const QString &loggedInEmail);
    void processPresenceState(const QJsonObject &payload);

signals:
    void messageReceived(const QString &conversationId, const Message &message);
    void messageRecalled(const QString &conversationId, qint64 messageId);
    void messageSent(const QString &conversationId, const QString &clientMessageId);
    void messageFailed(const QString &conversationId, const QString &clientMessageId);
    void networkStatusChanged(const QString &status, const QString &detail = QString());
    void scrollMessagesToBottom();
    void favoriteHighlightsRefreshed();
    void typingUsersUpdated(const QString &conversationId, const QHash<QString, QString> &users);
    void presenceUpdated(const QString &conversationId, const QString &userEmail, bool isOnline, qint64 lastSeenAt);
    void realtimeConnected();
    void conversationJoined(const QString &conversationId);
    void profileUpdatedFromServer(const QString &email, const QString &nickname, const QString &avatarUrl);
    void serverError(const QString &clientMessageId, const QString &conversationId, const QString &message);
    void focusMessageRequested(const QString &conversationId, qint64 serverMessageId);

private:
    ChatStore *m_chatStore = nullptr;
    ChatClient *m_chatClient = nullptr;
    NetworkService *m_networkService = nullptr;

    QString m_loggedInEmail;
    QHash<QString, QString> m_conversationDrafts;
    QHash<QString, QJsonObject> m_favoriteMessagesByKey;
    QHash<QString, QTimer *> m_pendingMessageTimers;
    QHash<QString, QString> m_pendingMessageConversationIds;
    QHash<QString, qint64> m_lastReadAckMessageIds;
    QHash<QString, QHash<QString, QString>> m_typingUsersByConversationId;
    int m_messageLoadSerial = 0;
};
