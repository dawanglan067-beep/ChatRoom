#pragma once

#include <QObject>
#include <QHash>

#include "conversation.h"

class ChatStore : public QObject
{
    Q_OBJECT

public:
    explicit ChatStore(QObject *parent = nullptr);

    const QList<Conversation> &conversations() const;
    const Conversation *conversationAt(int index) const;
    const Conversation *currentConversation() const;
    int currentConversationIndex() const;
    int currentMessageCount() const;
    int queuedMessageCount() const;
    const Message *messageAt(int index) const;
    int findConversationIndex(const QString &conversationId) const;

    void clear();
    void replaceConversations(QList<Conversation> conversations);
    bool replaceMessagesForConversation(int index, QList<Message> messages);
    bool prependMessagesForConversation(int index, QList<Message> olderMessages);
    bool appendMessageToConversation(const QString &conversationId, const Message &message);
    bool addPendingMessageToCurrentChat(const QString &content, const QString &clientMessageId);
    bool setConversationDraft(const QString &conversationId, const QString &draftText);
    bool markMessageQueued(const QString &conversationId, const QString &clientMessageId);
    bool markMessageSending(const QString &conversationId, const QString &clientMessageId);
    bool markMessageFailed(const QString &conversationId, const QString &clientMessageId);
    bool removeQueuedMessage(const QString &conversationId, const QString &clientMessageId);
    bool markMessagesReadByPeer(const QString &conversationId, qint64 lastReadServerMessageId);
    bool markMessageRecalled(const QString &conversationId, qint64 serverMessageId, const Message &recalledMessage);
    bool markPendingMessageSent(const QString &conversationId, const QString &clientMessageId,
                                const Message &serverMessage);
    bool markConversationRead(int index);
    bool markConversationReadById(const QString &conversationId);
    void setCurrentConversation(int index);
    bool addReceivedMessageToCurrentChat(const QString &content, const QString &senderId);

signals:
    void conversationsReset();
    void currentConversationChanged();
    void messagesPrepended(int count);
    void messageAppended(int index);
    void messageUpdated(int index);
    void conversationUpdated(int index);

private:
    void rebuildIndexCache();
    void recalculateQueuedMessageCount();

    QList<Conversation> m_conversations;
    mutable QHash<QString, int> m_conversationIndexCache;
    int m_currentConversationIndex = -1;
    int m_queuedMessageCount = 0;
};
