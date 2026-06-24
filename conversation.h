#pragma once

#include <QList>
#include <QString>

#include "message.h"

class Conversation
{
public:
    enum class Type {
        Group,
        Direct
    };

    Conversation() = default;
    Conversation(QString idValue, QString nameValue, QList<Message> messageList = {}, int unreadCountValue = 0)
        : id(std::move(idValue))
        , name(std::move(nameValue))
        , messages(std::move(messageList))
        , unreadCount(unreadCountValue)
    {
    }

    static Type typeFromString(const QString &str) {
        return str == QStringLiteral("direct") ? Type::Direct : Type::Group;
    }

    static QString typeToString(Type t) {
        return t == Type::Direct ? QStringLiteral("direct") : QStringLiteral("group");
    }

    bool isGroup() const { return type == Type::Group; }
    bool isDirect() const { return type == Type::Direct; }

    QString id;
    QString name;
    Type type = Type::Group;
    QString ownerEmail;
    QString lastMessagePreview;
    QString draftText;
    qint64 lastMessageTimestamp = 0;
    QList<Message> messages;
    int memberCount = 0;
    int onlineCount = 0;
    int unreadCount = 0;
};
