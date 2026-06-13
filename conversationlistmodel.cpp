#include "conversationlistmodel.h"

#include "timeformatutils.h"
#include "uitexts.h"

#include <QRegularExpression>
#include <QtGlobal>

namespace
{
QString collapsePreviewWhitespace(QString text)
{
    text.replace(QRegularExpression(QStringLiteral("[\\t\\r\\n ]+")), QStringLiteral(" "));
    return text.trimmed();
}

bool parseMediaPreviewLine(const QString &firstLine, bool *isImageOut, QString *fileNameOut)
{
    static const QRegularExpression kMediaPrefixRegex(
        QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch prefixMatch = kMediaPrefixRegex.match(firstLine.trimmed());
    if (!prefixMatch.hasMatch()) {
        return false;
    }

    const QString mediaTag = prefixMatch.captured(1).trimmed().toLower();
    const bool isImage = mediaTag == QStringLiteral("图片")
        || mediaTag == QStringLiteral("image")
        || mediaTag == QStringLiteral("img")
        || mediaTag == QStringLiteral("photo");
    const bool isFile = mediaTag == QStringLiteral("文件")
        || mediaTag == QStringLiteral("file")
        || mediaTag == QStringLiteral("attachment");
    if (!isImage && !isFile) {
        return false;
    }

    if (isImageOut) {
        *isImageOut = isImage;
    }
    if (fileNameOut) {
        *fileNameOut = prefixMatch.captured(2).trimmed();
    }
    return true;
}

QString formatMediaPreviewText(const QString &messageBody)
{
    const QStringList lines = messageBody.split(QRegularExpression(QStringLiteral("[\\r\\n]+")),
                                                Qt::SkipEmptyParts);
    if (lines.isEmpty()) {
        return QString();
    }

    bool isImage = false;
    QString fileName;
    if (!parseMediaPreviewLine(lines.constFirst(), &isImage, &fileName)) {
        return QString();
    }

    if (fileName.isEmpty()) {
        return isImage ? QStringLiteral("[图片]") : QStringLiteral("[文件]");
    }
    return isImage
        ? QStringLiteral("[图片] %1").arg(fileName)
        : QStringLiteral("[文件] %1").arg(fileName);
}

QString normalizePreviewText(QString text)
{
    text = text.trimmed();
    if (text.isEmpty()) {
        return text;
    }

    static const QRegularExpression kSelfPrefixRegex(QStringLiteral("^你\\s*[:：]\\s*(.*)$"));
    const QRegularExpressionMatch selfMatch = kSelfPrefixRegex.match(text);
    if (selfMatch.hasMatch()) {
        const QString selfBody = selfMatch.captured(1).trimmed();
        const QString mediaText = formatMediaPreviewText(selfBody);
        const QString normalizedBody = mediaText.isEmpty() ? collapsePreviewWhitespace(selfBody) : mediaText;
        return QStringLiteral("你: %1").arg(normalizedBody);
    }

    const QString mediaText = formatMediaPreviewText(text);
    if (!mediaText.isEmpty()) {
        return mediaText;
    }

    text = collapsePreviewWhitespace(text);
    if (text.isEmpty()) {
        return text;
    }

    if (text.startsWith(UiText::MessageBubble::kSystemPrefix)) {
        const QString systemBody = text.mid(UiText::MessageBubble::kSystemPrefix.size()).trimmed();
        return QStringLiteral("\u7cfb\u7edf\u6d88\u606f: %1").arg(systemBody);
    }

    return text;
}

QString formatSelfMessagePreview(const Message &message)
{
    QString statusSuffix;
    switch (message.status) {
    case Message::DeliveryStatus::Sending:
        statusSuffix = UiText::MessageBubble::kSending;
        break;
    case Message::DeliveryStatus::Queued:
        statusSuffix = UiText::MessageBubble::kQueued;
        break;
    case Message::DeliveryStatus::Delivered:
        statusSuffix = UiText::MessageBubble::kDelivered;
        break;
    case Message::DeliveryStatus::Read:
        statusSuffix = UiText::MessageBubble::kRead;
        break;
    case Message::DeliveryStatus::Failed:
        statusSuffix = UiText::MessageBubble::kFailed;
        break;
    case Message::DeliveryStatus::Sent:
    default:
        break;
    }

    const QString baseText =
        QStringLiteral("\u4f60: %1").arg(normalizePreviewText(message.content));
    return statusSuffix.isEmpty()
        ? baseText
        : QStringLiteral("%1\uff08%2\uff09").arg(baseText, statusSuffix);
}

QString formatPresenceSummary(const Conversation &conversation)
{
    if (conversation.memberCount <= 0) {
        return QString();
    }

    const int safeOnline = qMax(0, qMin(conversation.onlineCount, conversation.memberCount));
    return QStringLiteral("\u5728\u7ebf %1/%2").arg(safeOnline).arg(conversation.memberCount);
}
}

ConversationListModel::ConversationListModel(ChatStore *chatStore, QObject *parent)
    : QAbstractListModel(parent)
    , m_chatStore(chatStore)
{
    connect(m_chatStore, &ChatStore::conversationsReset, this, [this]() {
        beginResetModel();
        endResetModel();
    });

    connect(m_chatStore, &ChatStore::conversationUpdated, this, [this](int row) {
        const QModelIndex changedIndex = index(row, 0);
        emit dataChanged(changedIndex, changedIndex);
    });
}

int ConversationListModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid() || !m_chatStore) {
        return 0;
    }
    return m_chatStore->conversations().size();
}

QVariant ConversationListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || !m_chatStore) {
        return {};
    }

    const Conversation *conversation = m_chatStore->conversationAt(index.row());
    if (!conversation) {
        return {};
    }

    const Message *lastMessage =
        conversation->messages.isEmpty() ? nullptr : &conversation->messages.constLast();
    const bool hasDraft = !conversation->draftText.trimmed().isEmpty();
    const QString previewText = lastMessage
        ? (lastMessage->isSelf ? formatSelfMessagePreview(*lastMessage)
                               : normalizePreviewText(lastMessage->content))
        : normalizePreviewText(conversation->lastMessagePreview);
    const QString finalPreviewText = hasDraft
        ? QStringLiteral("\u3010\u8349\u7a3f\u3011 %1").arg(normalizePreviewText(conversation->draftText))
        : previewText;
    const qint64 previewTimestamp =
        lastMessage ? lastMessage->timestamp : conversation->lastMessageTimestamp;

    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return conversation->name;
    case IdRole:
        return conversation->id;
    case LastMessageRole:
        return finalPreviewText;
    case LastMessageTimeRole:
        return hasDraft ? QStringLiteral("\u8349\u7a3f") : formatConversationTimeLabel(previewTimestamp);
    case MessageCountRole:
        return conversation->messages.size();
    case UnreadCountRole:
        return conversation->unreadCount;
    case MemberCountRole:
        return conversation->memberCount;
    case OnlineCountRole:
        return conversation->onlineCount;
    case PresenceSummaryRole:
        return formatPresenceSummary(*conversation);
    default:
        return {};
    }
}

QHash<int, QByteArray> ConversationListModel::roleNames() const
{
    return {
        { IdRole, "id" },
        { NameRole, "name" },
        { LastMessageRole, "lastMessage" },
        { LastMessageTimeRole, "lastMessageTime" },
        { MessageCountRole, "messageCount" },
        { UnreadCountRole, "unreadCount" },
        { MemberCountRole, "memberCount" },
        { OnlineCountRole, "onlineCount" },
        { PresenceSummaryRole, "presenceSummary" },
    };
}
