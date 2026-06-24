#include "chatstore.h"
#include "uitexts.h"

#include <QDateTime>
#include <QSet>

namespace
{
void updateConversationPreviewFromLastMessage(Conversation *conversation)
{
    if (!conversation || conversation->messages.isEmpty()) {
        return;
    }

    const Message &lastMessage = conversation->messages.constLast();
    conversation->lastMessagePreview = lastMessage.content;
    conversation->lastMessageTimestamp = lastMessage.timestamp;
}
}

ChatStore::ChatStore(QObject *parent)
    : QObject(parent)
{
}

const QList<Conversation> &ChatStore::conversations() const
{
    return m_conversations;
}

const Conversation *ChatStore::conversationAt(int index) const
{
    if (index < 0 || index >= m_conversations.size()) {
        return nullptr;
    }
    return &m_conversations.at(index);
}

const Conversation *ChatStore::currentConversation() const
{
    return conversationAt(m_currentConversationIndex);
}

int ChatStore::currentConversationIndex() const
{
    return m_currentConversationIndex;
}

int ChatStore::currentMessageCount() const
{
    const Conversation *conversation = currentConversation();
    return conversation ? conversation->messages.size() : 0;
}

int ChatStore::queuedMessageCount() const
{
    return m_queuedMessageCount;
}

void ChatStore::recalculateQueuedMessageCount()
{
    int count = 0;
    for (const Conversation &conversation : m_conversations) {
        for (const Message &message : conversation.messages) {
            if (message.isSelf && message.status == Message::DeliveryStatus::Queued) {
                count += 1;
            }
        }
    }
    m_queuedMessageCount = count;
}

const Message *ChatStore::messageAt(int index) const
{
    const Conversation *conversation = currentConversation();
    if (!conversation || index < 0 || index >= conversation->messages.size()) {
        return nullptr;
    }
    return &conversation->messages.at(index);
}

int ChatStore::findConversationIndex(const QString &conversationId) const
{
    if (conversationId.isEmpty()) {
        return -1;
    }

    const auto it = m_conversationIndexCache.constFind(conversationId);
    if (it != m_conversationIndexCache.constEnd()) {
        const int index = it.value();
        if (index >= 0 && index < m_conversations.size() && m_conversations.at(index).id == conversationId) {
            return index;
        }
    }

    for (int index = 0; index < m_conversations.size(); ++index) {
        if (m_conversations.at(index).id == conversationId) {
            m_conversationIndexCache.insert(conversationId, index);
            return index;
        }
    }
    return -1;
}

void ChatStore::rebuildIndexCache()
{
    m_conversationIndexCache.clear();
    m_conversationIndexCache.reserve(m_conversations.size());
    for (int index = 0; index < m_conversations.size(); ++index) {
        m_conversationIndexCache.insert(m_conversations.at(index).id, index);
    }
}

void ChatStore::clear()
{
    m_conversations.clear();
    m_conversationIndexCache.clear();
    m_currentConversationIndex = -1;
    m_queuedMessageCount = 0;

    emit conversationsReset();
    emit currentConversationChanged();
}

void ChatStore::replaceConversations(QList<Conversation> conversations)
{
    m_conversations = std::move(conversations);
    rebuildIndexCache();
    recalculateQueuedMessageCount();

    if (m_conversations.isEmpty()) {
        m_currentConversationIndex = -1;
    } else if (m_currentConversationIndex < 0 || m_currentConversationIndex >= m_conversations.size()) {
        m_currentConversationIndex = 0;
    }

    emit conversationsReset();
    emit currentConversationChanged();
}

bool ChatStore::replaceMessagesForConversation(int index, QList<Message> messages)
{
    if (index < 0 || index >= m_conversations.size()) {
        return false;
    }

    m_conversations[index].messages = std::move(messages);
    updateConversationPreviewFromLastMessage(&m_conversations[index]);
    m_conversations[index].unreadCount = 0;
    recalculateQueuedMessageCount();
    emit conversationUpdated(index);

    if (index == m_currentConversationIndex) {
        emit currentConversationChanged();
    }

    return true;
}

bool ChatStore::prependMessagesForConversation(int index, QList<Message> olderMessages)
{
    if (index < 0 || index >= m_conversations.size()) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    if (olderMessages.isEmpty()) {
        return false;
    }

    QSet<qint64> existingServerMessageIds;
    existingServerMessageIds.reserve(conversation.messages.size() + olderMessages.size());
    for (const Message &message : conversation.messages) {
        if (message.serverMessageId > 0) {
            existingServerMessageIds.insert(message.serverMessageId);
        }
    }

    QList<Message> filteredOlderMessages;
    filteredOlderMessages.reserve(olderMessages.size());
    for (const Message &message : olderMessages) {
        if (message.serverMessageId > 0 && existingServerMessageIds.contains(message.serverMessageId)) {
            continue;
        }
        if (message.serverMessageId > 0) {
            existingServerMessageIds.insert(message.serverMessageId);
        }
        filteredOlderMessages.append(message);
    }

    if (filteredOlderMessages.isEmpty()) {
        return false;
    }

    const int prependedCount = filteredOlderMessages.size();
    filteredOlderMessages.append(conversation.messages);
    conversation.messages = std::move(filteredOlderMessages);
    updateConversationPreviewFromLastMessage(&conversation);
    emit conversationUpdated(index);

    if (index == m_currentConversationIndex) {
        emit messagesPrepended(prependedCount);
    }

    return true;
}

bool ChatStore::appendMessageToConversation(const QString &conversationId, const Message &message)
{
    if (conversationId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    const QString currentConversationId = currentConversation() ? currentConversation()->id : QString();

    Conversation &conversation = m_conversations[index];
    const int newMessageIndex = conversation.messages.size();
    conversation.messages.append(message);
    updateConversationPreviewFromLastMessage(&conversation);
    if (conversationId != currentConversationId && !message.isSelf) {
        conversation.unreadCount += 1;
    }

    if (index == 0) {
        if (index == m_currentConversationIndex) {
            emit messageAppended(newMessageIndex);
        }
        emit conversationUpdated(index);
        return true;
    }

    Conversation updatedConversation = m_conversations.takeAt(index);
    const int msgIndex = updatedConversation.messages.size() - 1;
    m_conversations.prepend(std::move(updatedConversation));
    rebuildIndexCache();

    if (!currentConversationId.isEmpty()) {
        m_currentConversationIndex = findConversationIndex(currentConversationId);
    } else {
        m_currentConversationIndex = 0;
    }

    emit conversationsReset();
    if (m_currentConversationIndex == 0 && msgIndex >= 0) {
        emit messageAppended(msgIndex);
    }
    return true;
}

bool ChatStore::addPendingMessageToCurrentChat(const QString &content, const QString &clientMessageId)
{
    const QString trimmedContent = content.trimmed();
    if (trimmedContent.isEmpty() || clientMessageId.trimmed().isEmpty() || m_currentConversationIndex < 0
        || m_currentConversationIndex >= m_conversations.size()) {
        return false;
    }

    Conversation &conversation = m_conversations[m_currentConversationIndex];
    const int newMessageIndex = conversation.messages.size();
    conversation.messages.append(
        Message(trimmedContent,
                QDateTime::currentMSecsSinceEpoch(),
                QStringLiteral("me"),
                true,
                Message::DeliveryStatus::Sending,
                clientMessageId));
    updateConversationPreviewFromLastMessage(&conversation);

    emit messageAppended(newMessageIndex);
    emit conversationUpdated(m_currentConversationIndex);
    return true;
}

bool ChatStore::setConversationDraft(const QString &conversationId, const QString &draftText)
{
    if (conversationId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    const QString normalizedDraft = draftText;
    if (conversation.draftText == normalizedDraft) {
        return false;
    }

    conversation.draftText = normalizedDraft;
    emit conversationUpdated(index);
    return true;
}

bool ChatStore::markMessageFailed(const QString &conversationId, const QString &clientMessageId)
{
    if (conversationId.trimmed().isEmpty() || clientMessageId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        Message &message = conversation.messages[row];
        if (message.clientMessageId != clientMessageId || !message.isSelf) {
            continue;
        }

        if (message.status == Message::DeliveryStatus::Queued) {
            m_queuedMessageCount = qMax(0, m_queuedMessageCount - 1);
        }
        message.status = Message::DeliveryStatus::Failed;
        emit messageUpdated(row);
        emit conversationUpdated(index);
        return true;
    }

    return false;
}

bool ChatStore::markMessageQueued(const QString &conversationId, const QString &clientMessageId)
{
    if (conversationId.trimmed().isEmpty() || clientMessageId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        Message &message = conversation.messages[row];
        if (message.clientMessageId != clientMessageId || !message.isSelf) {
            continue;
        }

        message.status = Message::DeliveryStatus::Queued;
        m_queuedMessageCount += 1;
        emit messageUpdated(row);
        emit conversationUpdated(index);
        return true;
    }

    return false;
}

bool ChatStore::markMessageSending(const QString &conversationId, const QString &clientMessageId)
{
    if (conversationId.trimmed().isEmpty() || clientMessageId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        Message &message = conversation.messages[row];
        if (message.clientMessageId != clientMessageId || !message.isSelf) {
            continue;
        }

        if (message.status == Message::DeliveryStatus::Delivered
            || message.status == Message::DeliveryStatus::Read) {
            return true;
        }

        if (message.status == Message::DeliveryStatus::Queued) {
            m_queuedMessageCount = qMax(0, m_queuedMessageCount - 1);
        }
        message.status = Message::DeliveryStatus::Sending;
        message.timestamp = QDateTime::currentMSecsSinceEpoch();
        updateConversationPreviewFromLastMessage(&conversation);
        emit messageUpdated(row);
        emit conversationUpdated(index);
        return true;
    }

    return false;
}

bool ChatStore::removeQueuedMessage(const QString &conversationId, const QString &clientMessageId)
{
    if (conversationId.trimmed().isEmpty() || clientMessageId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        const Message &message = conversation.messages.at(row);
        if (!message.isSelf || message.clientMessageId != clientMessageId
            || message.status != Message::DeliveryStatus::Queued) {
            continue;
        }

        conversation.messages.removeAt(row);
        m_queuedMessageCount = qMax(0, m_queuedMessageCount - 1);
        updateConversationPreviewFromLastMessage(&conversation);
        emit conversationUpdated(index);
        if (index == m_currentConversationIndex) {
            emit currentConversationChanged();
        }
        return true;
    }

    return false;
}

bool ChatStore::markPendingMessageSent(const QString &conversationId, const QString &clientMessageId,
                                       const Message &serverMessage)
{
    if (conversationId.trimmed().isEmpty()) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    if (!clientMessageId.trimmed().isEmpty()) {
        for (int row = conversation.messages.size() - 1; row >= 0; --row) {
            Message &message = conversation.messages[row];
            if (message.clientMessageId != clientMessageId || !message.isSelf) {
                continue;
            }

            message = serverMessage;
            message.status = Message::DeliveryStatus::Delivered;
            message.clientMessageId = clientMessageId;
            updateConversationPreviewFromLastMessage(&conversation);
            emit messageUpdated(row);
            emit conversationUpdated(index);
            return true;
        }
    }

    return appendMessageToConversation(conversationId, serverMessage);
}

bool ChatStore::markMessagesReadByPeer(const QString &conversationId, qint64 lastReadServerMessageId)
{
    if (conversationId.trimmed().isEmpty() || lastReadServerMessageId <= 0) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    bool changed = false;
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        Message &message = conversation.messages[row];
        if (!message.isSelf || message.serverMessageId <= 0 || message.serverMessageId > lastReadServerMessageId) {
            continue;
        }
        if (message.status == Message::DeliveryStatus::Failed
            || message.status == Message::DeliveryStatus::Queued
            || message.status == Message::DeliveryStatus::Sending
            || message.status == Message::DeliveryStatus::Read) {
            continue;
        }

        message.status = Message::DeliveryStatus::Read;
        emit messageUpdated(row);
        changed = true;
    }

    if (changed) {
        emit conversationUpdated(index);
    }
    return changed;
}

bool ChatStore::markMessageRecalled(const QString &conversationId, qint64 serverMessageId,
                                    const Message &recalledMessage)
{
    if (conversationId.trimmed().isEmpty() || serverMessageId <= 0) {
        return false;
    }

    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    for (int row = conversation.messages.size() - 1; row >= 0; --row) {
        Message &currentMessage = conversation.messages[row];
        if (currentMessage.serverMessageId != serverMessageId) {
            continue;
        }

        Message updatedMessage = recalledMessage;
        if (updatedMessage.timestamp <= 0) {
            updatedMessage.timestamp = currentMessage.timestamp;
        }
        if (updatedMessage.serverMessageId <= 0) {
            updatedMessage.serverMessageId = currentMessage.serverMessageId;
        }
        updatedMessage.isSelf = false;
        updatedMessage.senderId = UiText::MessageBubble::kSystemSenderKey;
        updatedMessage.status = Message::DeliveryStatus::Sent;
        updatedMessage.clientMessageId.clear();

        currentMessage = std::move(updatedMessage);
        updateConversationPreviewFromLastMessage(&conversation);
        emit messageUpdated(row);
        emit conversationUpdated(index);
        return true;
    }

    return false;
}

bool ChatStore::markConversationRead(int index)
{
    if (index < 0 || index >= m_conversations.size()) {
        return false;
    }

    Conversation &conversation = m_conversations[index];
    if (conversation.unreadCount == 0) {
        return false;
    }

    conversation.unreadCount = 0;
    emit conversationUpdated(index);
    return true;
}

bool ChatStore::markConversationReadById(const QString &conversationId)
{
    const int index = findConversationIndex(conversationId);
    if (index < 0) {
        return false;
    }
    return markConversationRead(index);
}

void ChatStore::setCurrentConversation(int index)
{
    if (index < 0 || index >= m_conversations.size()) {
        return;
    }
    if (index == m_currentConversationIndex && m_conversations[index].id == m_conversations.value(m_currentConversationIndex).id) {
        return;
    }

    m_currentConversationIndex = index;
    markConversationRead(index);
    emit currentConversationChanged();
}

bool ChatStore::addReceivedMessageToCurrentChat(const QString &content, const QString &senderId)
{
    const QString trimmedContent = content.trimmed();
    if (trimmedContent.isEmpty() || m_currentConversationIndex < 0
        || m_currentConversationIndex >= m_conversations.size()) {
        return false;
    }

    Conversation &conversation = m_conversations[m_currentConversationIndex];
    const int newMessageIndex = conversation.messages.size();
    conversation.messages.append(
        Message(trimmedContent, QDateTime::currentMSecsSinceEpoch(), senderId, false));
    updateConversationPreviewFromLastMessage(&conversation);

    emit messageAppended(newMessageIndex);
    emit conversationUpdated(m_currentConversationIndex);
    return true;
}
