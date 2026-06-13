#pragma once

#include <QAbstractListModel>

#include "chatstore.h"

class ConversationListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum ConversationRoles {
        IdRole = Qt::UserRole + 1,
        NameRole,
        LastMessageRole,
        LastMessageTimeRole,
        MessageCountRole,
        UnreadCountRole,
        MemberCountRole,
        OnlineCountRole,
        PresenceSummaryRole
    };

    explicit ConversationListModel(ChatStore *chatStore, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    ChatStore *m_chatStore = nullptr;
};
