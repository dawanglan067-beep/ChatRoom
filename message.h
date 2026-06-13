#pragma once

#include <QString>
#include <QtTypes>

class Message
{
public:
    enum class DeliveryStatus {
        Sending,
        Queued,
        Sent,
        Delivered,
        Read,
        Failed
    };

    Message() = default;
    Message(QString contentValue, qint64 timestampValue, QString senderIdValue, bool isSelfValue,
            DeliveryStatus statusValue = DeliveryStatus::Delivered, QString clientMessageIdValue = QString(),
            qint64 serverMessageIdValue = 0, QString senderAvatarUrlValue = QString())
        : content(std::move(contentValue))
        , timestamp(timestampValue)
        , senderId(std::move(senderIdValue))
        , isSelf(isSelfValue)
        , status(statusValue)
        , clientMessageId(std::move(clientMessageIdValue))
        , serverMessageId(serverMessageIdValue)
        , senderAvatarUrl(std::move(senderAvatarUrlValue))
    {
    }

    QString content;
    qint64 timestamp = 0;
    QString senderId;
    bool isSelf = false;
    DeliveryStatus status = DeliveryStatus::Delivered;
    QString clientMessageId;
    qint64 serverMessageId = 0;
    QString senderAvatarUrl;
};
