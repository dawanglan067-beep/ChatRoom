#pragma once

#include <QObject>
#include <QString>
#include <QList>

class Conversation;
class Message;

class DatabaseManager : public QObject
{
    Q_OBJECT

public:
    explicit DatabaseManager(QObject *parent = nullptr);
    ~DatabaseManager() override;

    bool open(const QString &userEmail);
    void close();
    bool isOpen() const;

    void saveConversations(const QList<Conversation> &conversations);
    QList<Conversation> loadConversations() const;

    void saveMessages(const QString &conversationId, const QList<Message> &messages);
    QList<Message> loadMessages(const QString &conversationId, int limit = 200) const;
    void appendMessage(const QString &conversationId, const Message &message);
    void updateMessage(const QString &conversationId, qint64 serverMessageId, const Message &message);

private:
    bool createTables();
    QString m_dbPath;
    QString m_connectionName;
};
