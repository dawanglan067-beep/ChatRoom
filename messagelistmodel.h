#pragma once

#include <QAbstractListModel>

#include "chatstore.h"

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum MessageRoles {
        ContentRole = Qt::UserRole + 1,
        TimestampRole,
        SenderIdRole,
        IsSelfRole,
        FormattedTimeRole,
        DeliveryStatusRole,
        IsRetryableRole,
        ServerMessageIdRole,
        SenderAvatarUrlRole
    };

    explicit MessageListModel(ChatStore *chatStore, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    ChatStore *m_chatStore = nullptr;
};
