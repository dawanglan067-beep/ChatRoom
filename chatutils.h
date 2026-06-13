#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

class Message;
class QPixmap;

namespace ChatUtils
{
inline qint64 parseBackendTimestamp(const QString &value)
{
    const QDateTime withMs = QDateTime::fromString(value, Qt::ISODateWithMs);
    if (withMs.isValid()) {
        return withMs.toMSecsSinceEpoch();
    }

    const QDateTime withoutMs = QDateTime::fromString(value, Qt::ISODate);
    return withoutMs.isValid() ? withoutMs.toMSecsSinceEpoch() : QDateTime::currentMSecsSinceEpoch();
}

QString profileInitial(const QString &nickname, const QString &email);
QPixmap circularAvatarPixmap(const QPixmap &source, int edge);
QString formatMessageTimeOrFallback(qint64 timestampMs, const QString &fallbackText);

Message messageFromBackendPayload(const QJsonObject &object, const QString &loggedInEmail);

namespace SecureStorage
{
QByteArray encryptString(const QString &plaintext);
QString decryptString(const QByteArray &ciphertext);
void secureSetValue(const QString &key, const QString &value);
QString secureGetValue(const QString &key);
}

}
