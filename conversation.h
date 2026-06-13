#pragma once

#include <QList>
#include <QString>

#include "message.h"

class Conversation
{
public:
    Conversation() = default;
    Conversation(QString idValue, QString nameValue, QList<Message> messageList = {}, int unreadCountValue = 0)
        : id(std::move(idValue))
        , name(std::move(nameValue))
        , messages(std::move(messageList))
        , unreadCount(unreadCountValue)
    {
    }

    QString id;
    QString name;
    QString type = QStringLiteral("group");
    QString ownerEmail;
    QString lastMessagePreview;
    QString draftText;
    qint64 lastMessageTimestamp = 0;
    QList<Message> messages;
    int memberCount = 0;
    int onlineCount = 0;
    int unreadCount = 0;
};
