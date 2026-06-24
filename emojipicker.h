#pragma once

#include <QObject>
#include <QPoint>

class QWidget;
class QLineEdit;
class QGridLayout;
class QLabel;

class EmojiPicker : public QObject
{
    Q_OBJECT

public:
    explicit EmojiPicker(QWidget *parent = nullptr);

    void showAt(const QPoint &globalPos);

signals:
    void emojiSelected(const QString &emoji);

private:
    void setupUi();
    void showCategory(int index);
    void filterEmojis(const QString &keyword);
    QWidget *parentWidget() const;

    QWidget *m_widget = nullptr;
    QLineEdit *m_searchInput = nullptr;
    QWidget *m_gridContainer = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QLabel *m_categoryLabel = nullptr;
    int m_currentCategory = 0;
};
