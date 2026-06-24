#include "databasemanager.h"
#include "conversation.h"
#include "message.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QRegularExpression>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

DatabaseManager::DatabaseManager(QObject *parent)
    : QObject(parent)
{
}

DatabaseManager::~DatabaseManager()
{
    close();
}

bool DatabaseManager::open(const QString &userEmail)
{
    if (userEmail.trimmed().isEmpty()) {
        return false;
    }

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qWarning() << "QSQLITE driver not available, database persistence disabled";
        return false;
    }

    const QString safeEmail = userEmail.trimmed().toLower().replace(QRegularExpression(QStringLiteral("[^a-z0-9]")), QStringLiteral("_"));
    const QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dataDir);
    m_dbPath = QDir(dataDir).filePath(QStringLiteral("chatroom_%1.db").arg(safeEmail));
    m_connectionName = QStringLiteral("chatroom_%1").arg(safeEmail);

    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
        db.setDatabaseName(m_dbPath);
        if (!db.open()) {
            return false;
        }

        QSqlQuery query(db);
        if (!query.exec(QStringLiteral("PRAGMA journal_mode=WAL"))) {
            qWarning() << "DatabaseManager: PRAGMA journal_mode failed:" << query.lastError().text();
        }
        if (!query.exec(QStringLiteral("PRAGMA foreign_keys=ON"))) {
            qWarning() << "DatabaseManager: PRAGMA foreign_keys failed:" << query.lastError().text();
        }
    }

    return createTables();
}

void DatabaseManager::close()
{
    if (m_connectionName.isEmpty()) {
        return;
    }
    {
        QSqlDatabase db = QSqlDatabase::database(m_connectionName);
        if (db.isOpen()) {
            db.close();
        }
    }
    QSqlDatabase::removeDatabase(m_connectionName);
    m_connectionName.clear();
}

bool DatabaseManager::isOpen() const
{
    if (m_connectionName.isEmpty()) {
        return false;
    }
    return QSqlDatabase::database(m_connectionName).isOpen();
}

bool DatabaseManager::createTables()
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return false;
    }

    QSqlQuery query(db);
    bool ok = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS conversations ("
                       "id TEXT PRIMARY KEY, "
                       "name TEXT, "
                       "type TEXT DEFAULT 'group', "
                       "owner_email TEXT, "
                       "last_message_preview TEXT, "
                       "last_message_timestamp INTEGER DEFAULT 0, "
                       "member_count INTEGER DEFAULT 0, "
                       "online_count INTEGER DEFAULT 0, "
                       "unread_count INTEGER DEFAULT 0"
                       ")"));
    if (!ok) {
        return false;
    }

    ok = query.exec(
        QStringLiteral("CREATE TABLE IF NOT EXISTS messages ("
                       "id INTEGER PRIMARY KEY AUTOINCREMENT, "
                       "conversation_id TEXT NOT NULL, "
                       "server_message_id INTEGER DEFAULT 0, "
                       "client_message_id TEXT, "
                       "content TEXT, "
                       "sender_id TEXT, "
                       "is_self INTEGER DEFAULT 0, "
                       "timestamp INTEGER DEFAULT 0, "
                       "status INTEGER DEFAULT 3, "
                       "sender_avatar_url TEXT, "
                       "UNIQUE(conversation_id, server_message_id, client_message_id)"
                       ")"));
    if (!ok) {
        return false;
    }

    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_conv ON messages(conversation_id, timestamp)"))) {
        qWarning() << "DatabaseManager: create idx_messages_conv failed:" << query.lastError().text();
    }
    if (!query.exec(QStringLiteral("CREATE INDEX IF NOT EXISTS idx_messages_server_id ON messages(conversation_id, server_message_id)"))) {
        qWarning() << "DatabaseManager: create idx_messages_server_id failed:" << query.lastError().text();
    }

    return true;
}

void DatabaseManager::saveConversations(const QList<Conversation> &conversations)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return;
    }

    if (!db.transaction()) {
        return;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO conversations "
        "(id, name, type, owner_email, last_message_preview, last_message_timestamp, member_count, online_count, unread_count) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    bool ok = true;
    for (const Conversation &conv : conversations) {
        query.addBindValue(conv.id);
        query.addBindValue(conv.name);
        query.addBindValue(Conversation::typeToString(conv.type));
        query.addBindValue(conv.ownerEmail);
        query.addBindValue(conv.lastMessagePreview);
        query.addBindValue(conv.lastMessageTimestamp);
        query.addBindValue(conv.memberCount);
        query.addBindValue(conv.onlineCount);
        query.addBindValue(conv.unreadCount);
        if (!query.exec()) {
            ok = false;
            break;
        }
    }

    if (ok) {
        db.commit();
    } else {
        db.rollback();
    }
}

QList<Conversation> DatabaseManager::loadConversations() const
{
    QList<Conversation> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return result;
    }

    QSqlQuery query(db);
    query.exec(QStringLiteral("SELECT id, name, type, owner_email, last_message_preview, last_message_timestamp, member_count, online_count, unread_count FROM conversations ORDER BY last_message_timestamp DESC"));

    while (query.next()) {
        Conversation conv;
        conv.id = query.value(0).toString();
        conv.name = query.value(1).toString();
        conv.type = Conversation::typeFromString(query.value(2).toString());
        conv.ownerEmail = query.value(3).toString();
        conv.lastMessagePreview = query.value(4).toString();
        conv.lastMessageTimestamp = query.value(5).toLongLong();
        conv.memberCount = query.value(6).toInt();
        conv.onlineCount = query.value(7).toInt();
        conv.unreadCount = query.value(8).toInt();
        result.append(std::move(conv));
    }

    return result;
}

void DatabaseManager::saveMessages(const QString &conversationId, const QList<Message> &messages)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen() || conversationId.isEmpty()) {
        return;
    }

    if (!db.transaction()) {
        return;
    }
    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO messages "
        "(conversation_id, server_message_id, client_message_id, content, sender_id, is_self, timestamp, status, sender_avatar_url) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));

    bool ok = true;
    for (const Message &msg : messages) {
        query.addBindValue(conversationId);
        query.addBindValue(msg.serverMessageId);
        query.addBindValue(msg.clientMessageId);
        query.addBindValue(msg.content);
        query.addBindValue(msg.senderId);
        query.addBindValue(msg.isSelf ? 1 : 0);
        query.addBindValue(msg.timestamp);
        query.addBindValue(static_cast<int>(msg.status));
        query.addBindValue(msg.senderAvatarUrl);
        if (!query.exec()) {
            ok = false;
            break;
        }
    }

    if (ok) {
        db.commit();
        trimOldMessages();
    } else {
        db.rollback();
    }
}

QList<Message> DatabaseManager::loadMessages(const QString &conversationId, int limit) const
{
    QList<Message> result;
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen() || conversationId.isEmpty()) {
        return result;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "SELECT server_message_id, client_message_id, content, sender_id, is_self, timestamp, status, sender_avatar_url "
        "FROM messages WHERE conversation_id = ? ORDER BY timestamp DESC LIMIT ?"));
    query.addBindValue(conversationId);
    query.addBindValue(limit);
    if (!query.exec()) {
        qWarning() << "DatabaseManager::loadMessages failed:" << query.lastError().text();
        return result;
    }

    QList<Message> reversed;
    while (query.next()) {
        Message msg;
        msg.serverMessageId = query.value(0).toLongLong();
        msg.clientMessageId = query.value(1).toString();
        msg.content = query.value(2).toString();
        msg.senderId = query.value(3).toString();
        msg.isSelf = query.value(4).toBool();
        msg.timestamp = query.value(5).toLongLong();
        msg.status = static_cast<Message::DeliveryStatus>(query.value(6).toInt());
        msg.senderAvatarUrl = query.value(7).toString();
        reversed.append(std::move(msg));
    }

    for (int i = reversed.size() - 1; i >= 0; --i) {
        result.append(std::move(reversed[i]));
    }

    return result;
}

void DatabaseManager::appendMessage(const QString &conversationId, const Message &message)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen() || conversationId.isEmpty()) {
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "INSERT OR REPLACE INTO messages "
        "(conversation_id, server_message_id, client_message_id, content, sender_id, is_self, timestamp, status, sender_avatar_url) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    query.addBindValue(conversationId);
    query.addBindValue(message.serverMessageId);
    query.addBindValue(message.clientMessageId);
    query.addBindValue(message.content);
    query.addBindValue(message.senderId);
    query.addBindValue(message.isSelf ? 1 : 0);
    query.addBindValue(message.timestamp);
    query.addBindValue(static_cast<int>(message.status));
    query.addBindValue(message.senderAvatarUrl);
    if (!query.exec()) {
        qWarning() << "DatabaseManager::appendMessage failed:" << query.lastError().text();
    }
}

void DatabaseManager::updateMessage(const QString &conversationId, qint64 serverMessageId, const Message &message)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen() || conversationId.isEmpty() || serverMessageId <= 0) {
        return;
    }

    QSqlQuery query(db);
    query.prepare(QStringLiteral(
        "UPDATE messages SET content = ?, sender_id = ?, is_self = ?, timestamp = ?, status = ?, sender_avatar_url = ? "
        "WHERE conversation_id = ? AND server_message_id = ?"));
    query.addBindValue(message.content);
    query.addBindValue(message.senderId);
    query.addBindValue(message.isSelf ? 1 : 0);
    query.addBindValue(message.timestamp);
    query.addBindValue(static_cast<int>(message.status));
    query.addBindValue(message.senderAvatarUrl);
    query.addBindValue(conversationId);
    query.addBindValue(serverMessageId);
    if (!query.exec()) {
        qWarning() << "DatabaseManager::updateMessage failed:" << query.lastError().text();
    }
}

void DatabaseManager::trimOldMessages(int maxMessagesPerConversation)
{
    QSqlDatabase db = QSqlDatabase::database(m_connectionName);
    if (!db.isOpen()) {
        return;
    }

    QSqlQuery query(db);
    if (!query.exec(QStringLiteral(
        "DELETE FROM messages WHERE id IN ("
        "  SELECT m.id FROM messages m"
        "  INNER JOIN ("
        "    SELECT conversation_id, MAX(id) AS max_id FROM messages GROUP BY conversation_id"
        "  ) latest ON m.conversation_id = latest.conversation_id"
        "  WHERE m.id < latest.max_id - %1"
        ")").arg(maxMessagesPerConversation))) {
        qWarning() << "DatabaseManager::trimOldMessages failed:" << query.lastError().text();
    }
}
