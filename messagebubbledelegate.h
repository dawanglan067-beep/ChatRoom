#pragma once

#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QStyledItemDelegate>

class MessageBubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageBubbleDelegate(QObject *parent = nullptr);
    void setFavoriteServerMessageIds(const QSet<qint64> &favoriteMessageIds);
    void setMediaThumbnail(qint64 serverMessageId, const QPixmap &thumbnail);
    void clearMediaThumbnails();
    void setSenderAvatarPixmap(const QString &avatarUrl, const QPixmap &pixmap);
    void setSelfAvatarPixmap(const QPixmap &pixmap);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int bubbleMaxWidth(const QWidget *widget) const;

    QSet<qint64> m_favoriteServerMessageIds;
    QHash<qint64, QPixmap> m_mediaThumbnailsByServerMessageId;
    QHash<QString, QPixmap> m_senderAvatarPixmapsByUrl;
    QPixmap m_selfAvatarPixmap;
};
