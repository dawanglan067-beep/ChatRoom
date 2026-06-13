#include "conversationitemdelegate.h"

#include "conversationlistmodel.h"

#include <QPainter>

namespace
{
constexpr int kItemHeight = 78;
constexpr int kHorizontalPadding = 16;
constexpr int kVerticalPadding = 12;
constexpr int kAvatarSize = 42;
constexpr int kUnreadDotMinWidth = 20;
constexpr int kUnreadDotHeight = 20;
constexpr int kPresenceDotSize = 8;
}

ConversationItemDelegate::ConversationItemDelegate(QObject *parent)
    : QStyledItemDelegate(parent)
{
}

void ConversationItemDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                                     const QModelIndex &index) const
{
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);

    const bool selected = option.state & QStyle::State_Selected;
    const QString name = index.data(ConversationListModel::NameRole).toString();
    const QString lastMessage = index.data(ConversationListModel::LastMessageRole).toString();
    const QString lastMessageTime = index.data(ConversationListModel::LastMessageTimeRole).toString();
    const QString presenceSummary = index.data(ConversationListModel::PresenceSummaryRole).toString();
    const int onlineCount = index.data(ConversationListModel::OnlineCountRole).toInt();
    const int unreadCount = index.data(ConversationListModel::UnreadCountRole).toInt();

    const QRect cardRect = option.rect.adjusted(6, 4, -6, -4);
    const QColor cardColor = selected ? QColor(QStringLiteral("#DBEAFE"))
                                      : QColor(QStringLiteral("#F8FAFC"));
    painter->setPen(Qt::NoPen);
    painter->setBrush(cardColor);
    painter->drawRoundedRect(cardRect, 16, 16);

    const QRect avatarRect(cardRect.left() + kHorizontalPadding,
                           cardRect.top() + (cardRect.height() - kAvatarSize) / 2,
                           kAvatarSize,
                           kAvatarSize);
    QLinearGradient avatarGradient(avatarRect.topLeft(), avatarRect.bottomRight());
    avatarGradient.setColorAt(0.0, QColor(QStringLiteral("#60A5FA")));
    avatarGradient.setColorAt(1.0, QColor(QStringLiteral("#2563EB")));
    painter->setBrush(avatarGradient);
    painter->drawEllipse(avatarRect);

    QFont avatarFont = option.font;
    avatarFont.setBold(true);
    painter->setFont(avatarFont);
    painter->setPen(Qt::white);
    painter->drawText(avatarRect, Qt::AlignCenter, name.left(1).toUpper());

    const int textLeft = avatarRect.right() + 12;
    const int textRightPadding = unreadCount > 0 ? 44 : 12;
    const QRect nameRect(textLeft,
                         cardRect.top() + kVerticalPadding - 1,
                         cardRect.width() - (textLeft - cardRect.left()) - textRightPadding,
                         24);
    const QRect detailRect(textLeft,
                           nameRect.bottom() + 4,
                           cardRect.width() - (textLeft - cardRect.left()) - textRightPadding,
                           20);

    QFont nameFont = option.font;
    nameFont.setBold(true);
    nameFont.setPointSizeF(option.font.pointSizeF() + 0.4);
    painter->setFont(nameFont);
    painter->setPen(QColor(QStringLiteral("#0F172A")));
    painter->drawText(nameRect, Qt::AlignLeft | Qt::AlignVCenter, name);
    if (onlineCount > 0) {
        const int dotY = nameRect.center().y() - (kPresenceDotSize / 2);
        QRect dotRect(nameRect.right() - kPresenceDotSize - 2, dotY, kPresenceDotSize, kPresenceDotSize);
        painter->setBrush(QColor(QStringLiteral("#22C55E")));
        painter->setPen(Qt::NoPen);
        painter->drawEllipse(dotRect);
    }

    QFont detailFont = option.font;
    detailFont.setPointSizeF(qMax(8.5, option.font.pointSizeF() - 0.4));
    painter->setFont(detailFont);
    painter->setPen(QColor(QStringLiteral("#64748B")));
    painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                      option.fontMetrics.elidedText(lastMessage, Qt::ElideRight, detailRect.width()));

    painter->drawText(QRect(cardRect.right() - 72, cardRect.top() + 10, 60, 18),
                      Qt::AlignRight | Qt::AlignVCenter,
                      lastMessageTime);
    if (!presenceSummary.isEmpty()) {
        QFont presenceFont = detailFont;
        presenceFont.setPointSizeF(qMax(8.0, detailFont.pointSizeF() - 0.2));
        painter->setFont(presenceFont);
        painter->setPen(QColor(QStringLiteral("#0F766E")));
        painter->drawText(QRect(cardRect.right() - 92, cardRect.top() + 28, 80, 16),
                          Qt::AlignRight | Qt::AlignVCenter,
                          presenceSummary);
    }

    if (unreadCount > 0) {
        const QString unreadText = unreadCount > 99 ? QStringLiteral("99+") : QString::number(unreadCount);
        const int unreadWidth = qMax(kUnreadDotMinWidth,
                                     painter->fontMetrics().horizontalAdvance(unreadText) + 10);
        const QRect unreadRect(cardRect.right() - unreadWidth - 12,
                               cardRect.bottom() - kUnreadDotHeight - 12,
                               unreadWidth,
                               kUnreadDotHeight);
        painter->setBrush(QColor(QStringLiteral("#EF4444")));
        painter->setPen(Qt::NoPen);
        painter->drawRoundedRect(unreadRect, 10, 10);

        QFont unreadFont = detailFont;
        unreadFont.setBold(true);
        unreadFont.setPointSizeF(qMax(8.0, detailFont.pointSizeF() - 0.2));
        painter->setFont(unreadFont);
        painter->setPen(Qt::white);
        painter->drawText(unreadRect, Qt::AlignCenter, unreadText);
    }

    painter->restore();
}

QSize ConversationItemDelegate::sizeHint(const QStyleOptionViewItem &option,
                                         const QModelIndex &index) const
{
    Q_UNUSED(option);
    Q_UNUSED(index);
    return QSize(0, kItemHeight);
}
