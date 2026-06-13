#include "messagelistmodel.h"

#include "timeformatutils.h"

MessageListModel::MessageListModel(ChatStore *chatStore, QObject *parent)
    : QAbstractListModel(parent)
    , m_chatStore(chatStore)
{
    connect(m_chatStore, &ChatStore::conversationsReset, this, [this]() {
        beginResetModel();
        endResetModel();
    });

    connect(m_chatStore, &ChatStore::currentConversationChanged, this, [this]() {
        beginResetModel();
        endResetModel();
    });

    connect(m_chatStore, &ChatStore::messageAppended, this, [this](int row) {
        beginInsertRows(QModelIndex(), row, row);
        endInsertRows();
    });

    connect(m_chatStore, &ChatStore::messagesPrepended, this, [this](int count) {
        if (count <= 0) {
            return;
        }
        beginInsertRows(QModelIndex(), 0, count - 1);
        endInsertRows();
    });

    connect(m_chatStore, &ChatStore::messageUpdated, this, [this](int row) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex);
    });
}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_chatStore) {
        return 0;
    }
    return m_chatStore->currentMessageCount();
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_chatStore) {
        return {};
    }

    const Message *message = m_chatStore->messageAt(index.row());
    if (!message) {
        return {};
    }

    switch (role) {
    case Qt::DisplayRole:
    case ContentRole:
        return message->content;
    case TimestampRole:
        return message->timestamp;
    case SenderIdRole:
        return message->senderId;
    case IsSelfRole:
        return message->isSelf;
    case FormattedTimeRole:
        return formatMessageTimeLabel(message->timestamp);
    case DeliveryStatusRole:
        return static_cast<int>(message->status);
    case IsRetryableRole:
        return message->isSelf && message->status == Message::DeliveryStatus::Failed;
    case ServerMessageIdRole:
        return message->serverMessageId;
    case SenderAvatarUrlRole:
        return message->senderAvatarUrl;
    default:
        return {};
    }
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {
        { ContentRole, "content" },
        { TimestampRole, "timestamp" },
        { SenderIdRole, "senderId" },
        { IsSelfRole, "isSelf" },
        { FormattedTimeRole, "formattedTime" },
        { DeliveryStatusRole, "deliveryStatus" },
        { IsRetryableRole, "isRetryable" },
        { ServerMessageIdRole, "serverMessageId" },
        { SenderAvatarUrlRole, "senderAvatarUrl" },
    };
}
