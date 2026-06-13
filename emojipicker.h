#pragma once

#include <QMenu>
#include <QObject>

class EmojiPicker : public QObject
{
    Q_OBJECT

public:
    explicit EmojiPicker(QWidget *parent = nullptr);

    void showAt(const QPoint &globalPos);

signals:
    void emojiSelected(const QString &emoji);

private:
    QMenu *m_menu = nullptr;
};
