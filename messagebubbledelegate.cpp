#include "messagebubbledelegate.h"

#include "messagelistmodel.h"
#include "uitexts.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <QRegularExpression>

namespace
{
constexpr int kOuterHorizontalMargin = 20;
constexpr int kOuterVerticalMargin = 6;
constexpr int kAvatarSize = 34;
constexpr int kAvatarGap = 10;
constexpr int kBubbleHorizontalPadding = 12;
constexpr int kBubbleTopPadding = 10;
constexpr int kBubbleBottomPadding = 22;
constexpr int kBubbleRadius = 16;
constexpr int kTailWidth = 10;
constexpr int kTailHeight = 12;
constexpr int kTailInset = 12;
constexpr int kSenderNameHeight = 18;
constexpr int kSystemHorizontalPadding = 12;
constexpr int kSystemVerticalPadding = 7;
constexpr int kImagePreviewWidth = 220;
constexpr int kImagePreviewHeight = 132;
constexpr int kFileCardWidth = 238;
constexpr int kFileCardHeight = 62;
constexpr int kFileIconSize = 38;

QString avatarTextForSender(const QString &senderId, bool isSelf)
{
    if (isSelf) {
        return UiText::MessageBubble::kSelfAvatar;
    }

    const QString trimmed = senderId.trimmed();
    if (trimmed.isEmpty()) {
        return UiText::MessageBubble::kFallbackAvatar;
    }

    return trimmed.left(1).toUpper();
}

bool isSystemMessage(const QString &senderId, const QString &content)
{
    return senderId.compare(UiText::MessageBubble::kSystemSenderKey, Qt::CaseInsensitive) == 0
        || content.startsWith(UiText::MessageBubble::kSystemPrefix);
}

QString formatFileSize(qint64 size)
{
    if (size < 0) {
        return QString();
    }
    if (size < 1024) {
        return QStringLiteral("%1 B").arg(size);
    }
    const double kb = static_cast<double>(size) / 1024.0;
    if (kb < 1024.0) {
        return QStringLiteral("%1 KB").arg(kb, 0, 'f', kb < 10.0 ? 1 : 0);
    }
    const double mb = kb / 1024.0;
    if (mb < 1024.0) {
        return QStringLiteral("%1 MB").arg(mb, 0, 'f', mb < 10.0 ? 1 : 0);
    }
    return QStringLiteral("%1 GB").arg(mb / 1024.0, 0, 'f', 1);
}

bool parseMediaMessageContent(const QString &content, bool *isImageOut, QString *nameOut, QString *urlOut,
                              qint64 *sizeOut = nullptr, QString *mimeTypeOut = nullptr)
{
    const QString normalized = content.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }

    const QStringList lines = normalized.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                               Qt::SkipEmptyParts);
    if (lines.size() < 2) {
        return false;
    }

    const QString firstLine = lines.at(0).trimmed();
    static const QRegularExpression kMediaPrefixRegex(
        QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch prefixMatch = kMediaPrefixRegex.match(firstLine);
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

    QString secondLine = lines.at(1).trimmed();
    static const QRegularExpression kUrlPrefixRegex(
        QStringLiteral("^(?:url|link|链接)\\s*[:：]\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    secondLine.remove(kUrlPrefixRegex);
    if (secondLine.isEmpty()) {
        return false;
    }

    if (isImageOut) {
        *isImageOut = isImage;
    }
    if (nameOut) {
        const QString fileName = prefixMatch.captured(2).trimmed();
        *nameOut = fileName.isEmpty() ? firstLine : fileName;
    }
    if (urlOut) {
        *urlOut = secondLine;
    }
    if (sizeOut) {
        *sizeOut = -1;
    }
    if (mimeTypeOut) {
        mimeTypeOut->clear();
    }
    static const QRegularExpression kSizeRegex(
        QStringLiteral("^(?:size|大小)\\s*[:：]\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kMimeRegex(
        QStringLiteral("^(?:mime|type|类型)\\s*[:：]\\s*(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    for (int i = 2; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const QRegularExpressionMatch sizeMatch = kSizeRegex.match(line);
        if (sizeMatch.hasMatch() && sizeOut) {
            *sizeOut = sizeMatch.captured(1).toLongLong();
            continue;
        }
        const QRegularExpressionMatch mimeMatch = kMimeRegex.match(line);
        if (mimeMatch.hasMatch() && mimeTypeOut) {
            *mimeTypeOut = mimeMatch.captured(1).trimmed();
        }
    }
    return true;
}

QPair<QColor, QColor> avatarGradientColors(const QString &senderId, bool isSelf)
{
    if (isSelf) {
        return { QColor(QStringLiteral("#2563EB")), QColor(QStringLiteral("#1D4ED8")) };
    }

    const uint seed = qHash(senderId.trimmed());
    switch (seed % 5) {
    case 0:
        return { QColor(QStringLiteral("#F59E0B")), QColor(QStringLiteral("#D97706")) };
    case 1:
        return { QColor(QStringLiteral("#10B981")), QColor(QStringLiteral("#059669")) };
    case 2:
        return { QColor(QStringLiteral("#8B5CF6")), QColor(QStringLiteral("#7C3AED")) };
    case 3:
        return { QColor(QStringLiteral("#EC4899")), QColor(QStringLiteral("#DB2777")) };
    default:
        return { QColor(QStringLiteral("#06B6D4")), QColor(QStringLiteral("#0891B2")) };
    }
}

void drawAvatar(QPainter *painter, const QRect &avatarRect, const QString &senderId, bool isSelf,
                const QFont &baseFont, const QPixmap &avatarPixmap = QPixmap())
{
    if (!avatarPixmap.isNull()) {
        const QPixmap scaled = avatarPixmap.scaled(avatarRect.size(),
                                                   Qt::KeepAspectRatioByExpanding,
                                                   Qt::SmoothTransformation);
        const QRect crop((scaled.width() - avatarRect.width()) / 2,
                         (scaled.height() - avatarRect.height()) / 2,
                         avatarRect.width(),
                         avatarRect.height());

        painter->save();
        QPainterPath avatarPath;
        avatarPath.addEllipse(avatarRect);
        painter->setClipPath(avatarPath);
        painter->drawPixmap(avatarRect.topLeft(), scaled.copy(crop));
        painter->restore();

        painter->setBrush(Qt::NoBrush);
        painter->setPen(QPen(QColor(255, 255, 255, 170), 1.4));
        painter->drawEllipse(avatarRect.adjusted(1, 1, -1, -1));
        return;
    }

    const auto [startColor, endColor] = avatarGradientColors(senderId, isSelf);

    QLinearGradient avatarGradient(avatarRect.topLeft(), avatarRect.bottomRight());
    avatarGradient.setColorAt(0.0, startColor);
    avatarGradient.setColorAt(1.0, endColor);

    painter->setPen(Qt::NoPen);
    painter->setBrush(avatarGradient);
    painter->drawEllipse(avatarRect);

    QRadialGradient highlight(avatarRect.center().x() - avatarRect.width() * 0.18,
                              avatarRect.top() + avatarRect.height() * 0.28,
                              avatarRect.width() * 0.6);
    highlight.setColorAt(0.0, QColor(255, 255, 255, 90));
    highlight.setColorAt(1.0, QColor(255, 255, 255, 0));
    painter->setBrush(highlight);
    painter->drawEllipse(avatarRect.adjusted(2, 2, -2, -2));

    painter->setBrush(Qt::NoBrush);
    painter->setPen(QPen(QColor(255, 255, 255, 120), 1.2));
    painter->drawEllipse(avatarRect.adjusted(1, 1, -1, -1));

    QFont avatarFont = baseFont;
    avatarFont.setBold(true);
    avatarFont.setPointSizeF(qMax(9.0, baseFont.pointSizeF()));
    painter->setFont(avatarFont);
    painter->setPen(Qt::white);
    painter->drawText(avatarRect, Qt::AlignCenter, avatarTextForSender(senderId, isSelf));
}
}

MessageBubbleDelegate::MessageBubbleDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void MessageBubbleDelegate::setFavoriteServerMessageIds(const QSet<qint64> &favoriteMessageIds)
{
    m_favoriteServerMessageIds = favoriteMessageIds;
}

void MessageBubbleDelegate::setSenderAvatarPixmap(const QString &avatarUrl, const QPixmap &pixmap)
{
    const QString normalizedUrl = avatarUrl.trimmed();
    if (normalizedUrl.isEmpty()) {
        return;
    }
    if (pixmap.isNull()) {
        m_senderAvatarPixmapsByUrl.remove(normalizedUrl);
        return;
    }
    m_senderAvatarPixmapsByUrl.insert(normalizedUrl, pixmap);
}

void MessageBubbleDelegate::setSelfAvatarPixmap(const QPixmap &pixmap)
{
    m_selfAvatarPixmap = pixmap;
}

void MessageBubbleDelegate::setMediaThumbnail(qint64 serverMessageId, const QPixmap &thumbnail)
{
    if (serverMessageId <= 0 || thumbnail.isNull()) {
        return;
    }
    m_mediaThumbnailsByServerMessageId.insert(serverMessageId, thumbnail);
}

void MessageBubbleDelegate::clearMediaThumbnails()
{
    m_mediaThumbnailsByServerMessageId.clear();
}

void MessageBubbleDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                   const QModelIndex &index) const
{
    if (!index.isValid() || !painter) {
        return;
    }
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    const QString content = index.data(MessageListModel::ContentRole).toString();
    const QString timeText = index.data(MessageListModel::FormattedTimeRole).toString();
    const QString senderId = index.data(MessageListModel::SenderIdRole).toString();
    const QString senderAvatarUrl = index.data(MessageListModel::SenderAvatarUrlRole).toString().trimmed();
    const auto deliveryStatus =
        static_cast<Message::DeliveryStatus>(index.data(MessageListModel::DeliveryStatusRole).toInt());
    const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
    const bool systemMessage = isSystemMessage(senderId, content);
    const bool showSenderName = !isSelf;
    const bool isFavorite = serverMessageId > 0 && m_favoriteServerMessageIds.contains(serverMessageId);

    const QRect itemRect =
        option.rect.adjusted(kOuterHorizontalMargin, kOuterVerticalMargin, -kOuterHorizontalMargin,
                             -kOuterVerticalMargin);

    if (systemMessage) {
        QFont systemFont = option.font;
        systemFont.setPointSizeF(qMax(8.5, option.font.pointSizeF() - 0.6));
        const QFontMetrics systemMetrics(systemFont);
        const QString systemText = content.startsWith(UiText::MessageBubble::kSystemPrefix)
            ? content.mid(UiText::MessageBubble::kSystemPrefix.size()).trimmed()
            : content;
        const int maxSystemWidth = qMin(260, itemRect.width() - 40);
        const QRect systemTextRect = systemMetrics.boundingRect(
            QRect(0, 0, maxSystemWidth, 200),
            Qt::TextWordWrap | Qt::AlignCenter,
            systemText);

        QRect systemRect(0, 0,
                         systemTextRect.width() + kSystemHorizontalPadding * 2,
                         systemTextRect.height() + kSystemVerticalPadding * 2);
        systemRect.moveCenter(QPoint(itemRect.center().x(), itemRect.top() + systemRect.height() / 2));

        painter->setPen(Qt::NoPen);
        painter->setBrush(QColor(QStringLiteral("#E5E7EB")));
        painter->drawRoundedRect(systemRect, 12, 12);

        painter->setFont(systemFont);
        painter->setPen(QColor(QStringLiteral("#6B7280")));
        painter->drawText(systemRect.adjusted(kSystemHorizontalPadding, kSystemVerticalPadding,
                                              -kSystemHorizontalPadding, -kSystemVerticalPadding),
                          Qt::TextWordWrap | Qt::AlignCenter,
                          systemText);
        painter->restore();
        return;
    }

    bool mediaIsImage = false;
    QString mediaName;
    QString mediaUrl;
    qint64 mediaSize = -1;
    QString mediaMimeType;
    const bool isMediaMessage = parseMediaMessageContent(content,
                                                         &mediaIsImage,
                                                         &mediaName,
                                                         &mediaUrl,
                                                         &mediaSize,
                                                         &mediaMimeType);
    Q_UNUSED(mediaUrl);
    Q_UNUSED(mediaMimeType);
    const bool isFileMessage = isMediaMessage && !mediaIsImage;
    const int maxTextWidth =
        qMax(120, bubbleMaxWidth(option.widget) - (kBubbleHorizontalPadding * 2));
    const QString textForLayout = isFileMessage ? QString() : (isMediaMessage ? mediaName : content);
    const QRect textRect = option.fontMetrics.boundingRect(
        QRect(0, 0, maxTextWidth, 10000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, textForLayout);

    QFont timeFont = option.font;
    timeFont.setPointSizeF(qMax(8.0, timeFont.pointSizeF() - 1.0));
    const QFontMetrics timeMetrics(timeFont);
    const int timeWidth = timeMetrics.horizontalAdvance(timeText);
    QString statusText;
    if (isSelf) {
        switch (deliveryStatus) {
        case Message::DeliveryStatus::Sending:
            statusText = UiText::MessageBubble::kSending;
            break;
        case Message::DeliveryStatus::Queued:
            statusText = UiText::MessageBubble::kQueued;
            break;
        case Message::DeliveryStatus::Delivered:
            statusText = UiText::MessageBubble::kDelivered;
            break;
        case Message::DeliveryStatus::Read:
            statusText = UiText::MessageBubble::kRead;
            break;
        case Message::DeliveryStatus::Failed:
            statusText = UiText::MessageBubble::kFailed;
            break;
        case Message::DeliveryStatus::Sent:
        default:
            break;
        }
    }
    const int statusWidth = statusText.isEmpty() ? 0 : timeMetrics.horizontalAdvance(statusText);

    const int previewHeight = (isMediaMessage && mediaIsImage) ? kImagePreviewHeight : 0;
    const int previewWidth = (isMediaMessage && mediaIsImage) ? kImagePreviewWidth : 0;
    const int fileCardHeight = isFileMessage ? kFileCardHeight : 0;
    const int fileCardWidth = isFileMessage ? kFileCardWidth : 0;
    const int bubbleWidth =
        qMin(bubbleMaxWidth(option.widget),
             qMax(qMax(qMax(textRect.width(), previewWidth), fileCardWidth),
                  timeWidth + (statusWidth > 0 ? statusWidth + 10 : 0))
                 + (kBubbleHorizontalPadding * 2));
    const int bubbleHeight =
        textRect.height() + previewHeight + fileCardHeight + kBubbleTopPadding + kBubbleBottomPadding
        + (isMediaMessage && !isFileMessage ? 6 : 0);
    const int bubbleTop = itemRect.top() + (showSenderName ? kSenderNameHeight : 0);

    QRect avatarRect(0, itemRect.top(), kAvatarSize, kAvatarSize);
    if (isSelf) {
        avatarRect.moveRight(itemRect.right());
    } else {
        avatarRect.moveLeft(itemRect.left());
    }

    QRect bubbleRect(0, bubbleTop, bubbleWidth, bubbleHeight);
    if (isSelf) {
        bubbleRect.moveRight(avatarRect.left() - kAvatarGap);
    } else {
        bubbleRect.moveLeft(avatarRect.right() + kAvatarGap);
    }

    const QColor bubbleColor = isSelf ? QColor(QStringLiteral("#0F9D58"))
                                      : QColor(QStringLiteral("#F2F4F8"));
    const QColor textColor = isSelf ? Qt::white : QColor(QStringLiteral("#1F2937"));
    const QColor timeColor = isSelf ? QColor(255, 255, 255, 180)
                                    : QColor(QStringLiteral("#6B7280"));
    painter->setPen(Qt::NoPen);
    painter->setBrush(bubbleColor);

    QPainterPath bubblePath;
    bubblePath.addRoundedRect(bubbleRect, kBubbleRadius, kBubbleRadius);

    QPolygon tail;
    if (isSelf) {
        tail << QPoint(bubbleRect.right() - kTailInset, bubbleRect.bottom() - kTailHeight)
             << QPoint(bubbleRect.right() - kTailInset, bubbleRect.bottom() - 4)
             << QPoint(bubbleRect.right() + kTailWidth, bubbleRect.bottom() - 8);
    } else {
        tail << QPoint(bubbleRect.left() + kTailInset, bubbleRect.bottom() - kTailHeight)
             << QPoint(bubbleRect.left() + kTailInset, bubbleRect.bottom() - 4)
             << QPoint(bubbleRect.left() - kTailWidth, bubbleRect.bottom() - 8);
    }

    QPainterPath tailPath;
    tailPath.addPolygon(tail);
    bubblePath = bubblePath.united(tailPath);
    painter->drawPath(bubblePath);

    if (showSenderName) {
        QFont senderFont = option.font;
        senderFont.setPointSizeF(qMax(8.5, option.font.pointSizeF() - 0.5));
        painter->setFont(senderFont);
        painter->setPen(QColor(QStringLiteral("#64748B")));
        const QRect senderRect(bubbleRect.left(), itemRect.top(), bubbleRect.width(), kSenderNameHeight);
        painter->drawText(senderRect, Qt::AlignLeft | Qt::AlignVCenter, senderId);
    }
    if (isFavorite) {
        QFont favoriteFont = option.font;
        favoriteFont.setBold(true);
        favoriteFont.setPointSizeF(qMax(9.0, option.font.pointSizeF() - 0.2));
        painter->setFont(favoriteFont);
        painter->setPen(QColor(QStringLiteral("#F59E0B")));
        const QRect favoriteRect = bubbleRect.adjusted(10, 6, -10, -6);
        painter->drawText(favoriteRect, Qt::AlignRight | Qt::AlignTop, QStringLiteral("★"));
    }

    QPixmap avatarPixmap;
    if (isSelf) {
        avatarPixmap = m_selfAvatarPixmap;
    } else {
        avatarPixmap = m_senderAvatarPixmapsByUrl.value(senderAvatarUrl);
        if (avatarPixmap.isNull()) {
            avatarPixmap = m_senderAvatarPixmapsByUrl.value(senderId.trimmed());
        }
    }
    drawAvatar(painter,
               avatarRect,
               senderId,
               isSelf,
               option.font,
               avatarPixmap);

    painter->setPen(textColor);
    painter->setFont(option.font);
    QRect contentRect =
        bubbleRect.adjusted(kBubbleHorizontalPadding, kBubbleTopPadding, -kBubbleHorizontalPadding,
                            -kBubbleBottomPadding);
    if (isMediaMessage && mediaIsImage) {
        const QRect previewRect(contentRect.left(),
                                contentRect.top(),
                                qMin(contentRect.width(), kImagePreviewWidth),
                                kImagePreviewHeight);
        painter->setBrush(QColor(isSelf ? QStringLiteral("#0B8150") : QStringLiteral("#E2E8F0")));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(previewRect, 10, 10);

        const QPixmap thumbnail = m_mediaThumbnailsByServerMessageId.value(serverMessageId);
        if (!thumbnail.isNull()) {
            const QPixmap scaled = thumbnail.scaled(previewRect.size(),
                                                     Qt::KeepAspectRatioByExpanding,
                                                     Qt::SmoothTransformation);
            painter->setClipRect(previewRect);
            painter->drawPixmap(previewRect.topLeft(), scaled,
                                QRect((scaled.width() - previewRect.width()) / 2,
                                      (scaled.height() - previewRect.height()) / 2,
                                      previewRect.width(),
                                      previewRect.height()));
            painter->setClipping(false);

            if (!mediaName.isEmpty()) {
                QFont nameFont = option.font;
                nameFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.0));
                painter->setFont(nameFont);
                painter->setPen(isSelf ? QColor(255, 255, 255, 200) : QColor(QStringLiteral("#64748B")));
                const QRect nameRect(previewRect.left() + 6, previewRect.bottom() - 22,
                                     previewRect.width() - 12, 18);
                const QString elidedName = option.fontMetrics.elidedText(mediaName, Qt::ElideMiddle, nameRect.width());
                painter->fillRect(QRect(nameRect.left() - 4, nameRect.top() - 2,
                                        nameRect.width() + 8, nameRect.height() + 4),
                                  QColor(0, 0, 0, 100));
                painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);
            }
        } else {
            QFont loadFont = option.font;
            loadFont.setPointSizeF(qMax(9.0, option.font.pointSizeF()));
            painter->setFont(loadFont);
            painter->setPen(isSelf ? QColor(255, 255, 255, 150) : QColor(QStringLiteral("#9ca3af")));
            painter->drawText(previewRect, Qt::AlignCenter, UiText::MessageBubble::kImageLoadingPlaceholder);
        }

        contentRect.setTop(previewRect.bottom() + 8);
    } else if (isFileMessage) {
        const QRect fileCardRect(contentRect.left(),
                                 contentRect.top(),
                                 qMin(contentRect.width(), kFileCardWidth),
                                 kFileCardHeight);
        painter->setPen(Qt::NoPen);
        painter->setBrush(isSelf ? QColor(255, 255, 255, 28) : QColor(QStringLiteral("#FFFFFF")));
        painter->drawRoundedRect(fileCardRect, 12, 12);

        const QRect iconRect(fileCardRect.left() + 10,
                             fileCardRect.top() + (fileCardRect.height() - kFileIconSize) / 2,
                             kFileIconSize,
                             kFileIconSize);
        painter->setBrush(isSelf ? QColor(255, 255, 255, 44) : QColor(QStringLiteral("#DBEAFE")));
        painter->drawRoundedRect(iconRect, 10, 10);

        QFont iconFont = option.font;
        iconFont.setBold(true);
        iconFont.setPointSizeF(qMax(10.0, option.font.pointSizeF() + 1.0));
        painter->setFont(iconFont);
        painter->setPen(isSelf ? QColor(255, 255, 255, 230) : QColor(QStringLiteral("#2563EB")));
        painter->drawText(iconRect, Qt::AlignCenter, QStringLiteral("F"));

        QFont fileNameFont = option.font;
        fileNameFont.setBold(true);
        painter->setFont(fileNameFont);
        painter->setPen(textColor);
        const QRect nameRect(iconRect.right() + 10,
                             fileCardRect.top() + 10,
                             fileCardRect.right() - iconRect.right() - 18,
                             20);
        const QString elidedName = option.fontMetrics.elidedText(mediaName, Qt::ElideMiddle, nameRect.width());
        painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, elidedName);

        QFont detailFont = option.font;
        detailFont.setPointSizeF(qMax(8.0, option.font.pointSizeF() - 1.0));
        painter->setFont(detailFont);
        painter->setPen(isSelf ? QColor(255, 255, 255, 170) : QColor(QStringLiteral("#64748B")));
        const QString sizeText = formatFileSize(mediaSize);
        const QString detailText = sizeText.isEmpty()
            ? UiText::MessageBubble::kClickToOpen
            : UiText::MessageBubble::kClickToOpenWithSizePattern.arg(sizeText);
        const QRect detailRect(nameRect.left(), nameRect.bottom() + 3, nameRect.width(), 18);
        painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter, detailText);

        painter->setFont(option.font);
        painter->setPen(textColor);
        contentRect.setTop(fileCardRect.bottom() + 8);
    }
    if (!textForLayout.isEmpty()) {
        painter->drawText(contentRect, Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, textForLayout);
    }

    painter->setFont(timeFont);
    painter->setPen(timeColor);
    const QRect timeRect = bubbleRect.adjusted(kBubbleHorizontalPadding,
                                               bubbleRect.height() - kBubbleBottomPadding + 4,
                                               -kBubbleHorizontalPadding, -6);
    painter->drawText(timeRect, Qt::AlignRight | Qt::AlignVCenter, timeText);

    if (!statusText.isEmpty()) {
        QColor statusColor = isSelf ? QColor(255, 255, 255, 180) : QColor(QStringLiteral("#6B7280"));
        if (deliveryStatus == Message::DeliveryStatus::Failed) {
            statusColor = QColor(QStringLiteral("#FCA5A5"));
        } else if (deliveryStatus == Message::DeliveryStatus::Read) {
            statusColor = QColor(QStringLiteral("#BBF7D0"));
        }
        painter->setPen(statusColor);
        const QRect statusRect = timeRect.adjusted(0, 0, -(timeWidth + 8), 0);
        painter->drawText(statusRect, Qt::AlignRight | Qt::AlignVCenter, statusText);
    }

    painter->restore();
}

QSize MessageBubbleDelegate::sizeHint(const QStyleOptionViewItem &option,
                                       const QModelIndex &index) const
{
    if (!index.isValid()) {
        return QSize(0, 0);
    }
    const QString content = index.data(MessageListModel::ContentRole).toString();
    const bool isSelf = index.data(MessageListModel::IsSelfRole).toBool();
    const QString senderId = index.data(MessageListModel::SenderIdRole).toString();

    if (isSystemMessage(senderId, content)) {
        QFont systemFont = option.font;
        systemFont.setPointSizeF(qMax(8.5, option.font.pointSizeF() - 0.6));
        const QFontMetrics systemMetrics(systemFont);
        const QString systemText = content.startsWith(UiText::MessageBubble::kSystemPrefix)
            ? content.mid(UiText::MessageBubble::kSystemPrefix.size()).trimmed()
            : content;
        const int maxSystemWidth = qMin(260, qMax(120, option.rect.width() - 80));
        const QRect systemTextRect = systemMetrics.boundingRect(
            QRect(0, 0, maxSystemWidth, 200),
            Qt::TextWordWrap | Qt::AlignCenter,
            systemText);
        return QSize(option.rect.width(),
                     systemTextRect.height() + kSystemVerticalPadding * 2 + kOuterVerticalMargin * 2);
    }

    const int maxTextWidth =
        qMax(120, bubbleMaxWidth(option.widget) - (kBubbleHorizontalPadding * 2));
    bool mediaIsImage = false;
    QString mediaName;
    QString mediaUrl;
    qint64 mediaSize = -1;
    QString mediaMimeType;
    const bool isMediaMessage = parseMediaMessageContent(content,
                                                         &mediaIsImage,
                                                         &mediaName,
                                                         &mediaUrl,
                                                         &mediaSize,
                                                         &mediaMimeType);
    Q_UNUSED(mediaUrl);
    Q_UNUSED(mediaSize);
    Q_UNUSED(mediaMimeType);
    const bool isFileMessage = isMediaMessage && !mediaIsImage;
    const QString textForLayout = isFileMessage ? QString() : (isMediaMessage ? mediaName : content);
    const QRect textRect = option.fontMetrics.boundingRect(
        QRect(0, 0, maxTextWidth, 10000), Qt::TextWordWrap | Qt::AlignLeft | Qt::AlignTop, textForLayout);

    const int previewHeight = (isMediaMessage && mediaIsImage) ? (kImagePreviewHeight + 6) : 0;
    const int fileCardHeight = isFileMessage ? kFileCardHeight : 0;
    const int bubbleHeight =
        textRect.height() + previewHeight + fileCardHeight + kBubbleTopPadding + kBubbleBottomPadding;
    const int totalHeight =
        qMax(bubbleHeight + (isSelf ? 0 : kSenderNameHeight), kAvatarSize) + (kOuterVerticalMargin * 2);
    return QSize(option.rect.width(), totalHeight);
}

int MessageBubbleDelegate::bubbleMaxWidth(const QWidget *widget) const
{
    if (!widget) {
        return 320;
    }

    return qBound(200, static_cast<int>(widget->width() * 0.42), 340);
}
