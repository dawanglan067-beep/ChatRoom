#include "chatutils.h"
#include "message.h"
#include "timeformatutils.h"
#include "uitexts.h"

#include <QPainter>
#include <QPainterPath>
#include <QPixmap>

namespace ChatUtils
{

QString profileInitial(const QString &nickname, const QString &email)
{
    const QString source = nickname.trimmed().isEmpty() ? email.trimmed() : nickname.trimmed();
    return source.isEmpty() ? QStringLiteral("?") : source.left(1).toUpper();
}

QPixmap circularAvatarPixmap(const QPixmap &source, int edge)
{
    if (source.isNull() || edge <= 0) {
        return QPixmap();
    }

    QPixmap scaled = source.scaled(edge, edge, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const QRect crop((scaled.width() - edge) / 2, (scaled.height() - edge) / 2, edge, edge);
    scaled = scaled.copy(crop);

    QPixmap rounded(edge, edge);
    rounded.fill(Qt::transparent);
    QPainter painter(&rounded);
    painter.setRenderHint(QPainter::Antialiasing, true);
    QPainterPath path;
    path.addEllipse(0, 0, edge, edge);
    painter.setClipPath(path);
    painter.drawPixmap(0, 0, scaled);
    return rounded;
}

QString formatMessageTimeOrFallback(qint64 timestampMs, const QString &fallbackText)
{
    const QString formatted = formatMessageTimeLabel(timestampMs);
    return formatted.isEmpty() ? fallbackText : formatted;
}

static QString recalledMessageDetail(const QString &recalledByEmail,
                                     const QString &recalledByNickname,
                                     const QString &loggedInEmail)
{
    const bool recalledBySelf = recalledByEmail.compare(loggedInEmail, Qt::CaseInsensitive) == 0;
    if (recalledBySelf) {
        return UiText::MainWindow::kRecalledBySelfMessage;
    }

    const QString displayName = recalledByNickname.trimmed().isEmpty()
        ? recalledByEmail.trimmed()
        : recalledByNickname.trimmed();
    if (displayName.isEmpty()) {
        return UiText::MainWindow::kRecalledByUnknownMessage;
    }
    return UiText::MainWindow::kRecalledByUserPattern.arg(displayName);
}

Message messageFromBackendPayload(const QJsonObject &object, const QString &loggedInEmail)
{
    const QString messageType = object.value(QStringLiteral("messageType")).toString().trimmed().toLower();
    const QString senderEmail = object.value(QStringLiteral("senderEmail")).toString();
    const QString senderNickname = object.value(QStringLiteral("senderNickname")).toString().trimmed();
    const QString senderAvatarUrl = object.value(QStringLiteral("senderAvatarUrl")).toString().trimmed();
    const QString senderDisplay = senderNickname.isEmpty() ? senderEmail.trimmed() : senderNickname;
    const QString text = object.value(QStringLiteral("content")).toString();
    const qint64 timestamp = parseBackendTimestamp(object.value(QStringLiteral("createdAt")).toString());
    const qint64 serverMessageId = object.value(QStringLiteral("id")).toInteger();

    if (messageType == QStringLiteral("system")) {
        QString systemText = text.trimmed();
        if (systemText == UiText::MainWindow::kBackendRecalledSystemText) {
            const QString recalledByEmail = senderEmail;
            const QString recalledByNickname = senderNickname;
            systemText = UiText::MainWindow::kSystemMessagePattern.arg(
                recalledMessageDetail(recalledByEmail, recalledByNickname, loggedInEmail));
        }
        if (!systemText.startsWith(UiText::MessageBubble::kSystemPrefix)) {
            systemText = QStringLiteral("%1 %2")
                             .arg(UiText::MessageBubble::kSystemPrefix, systemText);
        }

        return Message(systemText,
                       timestamp,
                       UiText::MessageBubble::kSystemSenderKey,
                       false,
                       Message::DeliveryStatus::Sent,
                       QString(),
                       serverMessageId);
    }

    const bool isSelf = senderEmail.compare(loggedInEmail, Qt::CaseInsensitive) == 0;
    const QString renderedText =
        (isSelf || senderDisplay.isEmpty())
        ? text
        : UiText::MainWindow::kSenderMessagePattern.arg(senderDisplay, text);
    return Message(renderedText,
                   timestamp,
                   senderDisplay,
                   isSelf,
                    isSelf ? Message::DeliveryStatus::Delivered : Message::DeliveryStatus::Sent,
                    QString(),
                    serverMessageId,
                    senderAvatarUrl);
}

}
