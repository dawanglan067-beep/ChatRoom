#pragma once

#include <QWidget>

class QGridLayout;
class QScrollArea;

class EmojiPicker : public QWidget
{
    Q_OBJECT

public:
    explicit EmojiPicker(QWidget *parent = nullptr);

signals:
    void emojiSelected(const QString &emoji);

private:
    void addCategory(const QString &label, const QStringList &emojis);
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_contentWidget = nullptr;
    QGridLayout *m_mainLayout = nullptr;
};
