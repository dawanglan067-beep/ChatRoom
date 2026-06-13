#pragma once

#include <QList>
#include <QHash>
#include <QPixmap>
#include <QSet>
#include <QStyledItemDelegate>

class MessageBubbleDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit MessageBubbleDelegate(QObject *parent = nullptr);
    void setSearchHighlight(const QList<int> &matchedRows, int currentRow);
    void setFavoriteServerMessageIds(const QSet<qint64> &favoriteMessageIds);
    void setMediaThumbnail(qint64 serverMessageId, const QPixmap &thumbnail);
    void clearMediaThumbnails();
    void setSelfAvatarPixmap(const QPixmap &pixmap);
    void setSenderAvatarPixmap(const QString &avatarUrl, const QPixmap &pixmap);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    int bubbleMaxWidth(const QWidget *widget) const;

    QSet<int> m_searchMatchedRows;
    int m_searchCurrentRow = -1;
    QSet<qint64> m_favoriteServerMessageIds;
    QHash<qint64, QPixmap> m_mediaThumbnailsByServerMessageId;
    QHash<QString, QPixmap> m_senderAvatarPixmapsByUrl;
    QPixmap m_selfAvatarPixmap;
};
