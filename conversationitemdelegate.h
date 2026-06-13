#pragma once

#include <QStyledItemDelegate>

class ConversationItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT

public:
    explicit ConversationItemDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;
};
