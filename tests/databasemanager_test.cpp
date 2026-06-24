#include <QtTest>
#include <QDateTime>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QStandardPaths>

#include "databasemanager.h"
#include "conversation.h"
#include "message.h"

class DatabaseManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void cleanupTestCase();
    void saveAndLoadConversations();
    void saveAndLoadMessages();
    void appendMessage();
    void updateMessage();
    void loadMessagesRespectsLimit();
    void trimOldMessages();

private:
    QTemporaryDir m_tempDir;
    DatabaseManager *m_db = nullptr;
};

void DatabaseManagerTest::initTestCase()
{
    QVERIFY(m_tempDir.isValid());
    m_db = new DatabaseManager(this);
    const QString uniqueEmail = QStringLiteral("test_%1@example.com").arg(QDateTime::currentMSecsSinceEpoch());
    QVERIFY(m_db->open(uniqueEmail));
    QVERIFY(m_db->isOpen());
}

void DatabaseManagerTest::cleanupTestCase()
{
    delete m_db;
    m_db = nullptr;
}

void DatabaseManagerTest::saveAndLoadConversations()
{
    QList<Conversation> conversations;
    Conversation conv1(QStringLiteral("c-1"), QStringLiteral("Conversation 1"));
    conv1.type = Conversation::Type::Direct;
    conv1.lastMessagePreview = QStringLiteral("Hello");
    conv1.lastMessageTimestamp = 1000;
    conv1.unreadCount = 3;
    conversations.append(conv1);

    Conversation conv2(QStringLiteral("c-2"), QStringLiteral("Conversation 2"));
    conv2.type = Conversation::Type::Group;
    conv2.memberCount = 5;
    conv2.onlineCount = 2;
    conversations.append(conv2);

    m_db->saveConversations(conversations);

    const QList<Conversation> loaded = m_db->loadConversations();
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).id, QStringLiteral("c-1"));
    QCOMPARE(loaded.at(0).name, QStringLiteral("Conversation 1"));
    QCOMPARE(loaded.at(0).type, Conversation::Type::Direct);
    QCOMPARE(loaded.at(0).unreadCount, 3);
    QCOMPARE(loaded.at(1).id, QStringLiteral("c-2"));
    QCOMPARE(loaded.at(1).memberCount, 5);
}

void DatabaseManagerTest::saveAndLoadMessages()
{
    QList<Message> messages;
    messages.append(Message(
        QStringLiteral("Hello"), 1000, QStringLiteral("user1"), false,
        Message::DeliveryStatus::Sent, QString(), 101));
    messages.append(Message(
        QStringLiteral("World"), 2000, QStringLiteral("me"), true,
        Message::DeliveryStatus::Delivered, QStringLiteral("client-1"), 102));

    m_db->saveMessages(QStringLiteral("c-msg"), messages);

    const QList<Message> loaded = m_db->loadMessages(QStringLiteral("c-msg"));
    QCOMPARE(loaded.size(), 2);
    QCOMPARE(loaded.at(0).content, QStringLiteral("Hello"));
    QCOMPARE(loaded.at(0).serverMessageId, 101LL);
    QVERIFY(!loaded.at(0).isSelf);
    QCOMPARE(loaded.at(1).content, QStringLiteral("World"));
    QVERIFY(loaded.at(1).isSelf);
    QCOMPARE(loaded.at(1).clientMessageId, QStringLiteral("client-1"));
}

void DatabaseManagerTest::appendMessage()
{
    Message msg(
        QStringLiteral("Appended"), 3000, QStringLiteral("user2"), false,
        Message::DeliveryStatus::Sent, QString(), 201);
    m_db->appendMessage(QStringLiteral("c-msg"), msg);

    const QList<Message> loaded = m_db->loadMessages(QStringLiteral("c-msg"));
    QVERIFY(loaded.size() >= 3);
    const Message &last = loaded.constLast();
    QCOMPARE(last.content, QStringLiteral("Appended"));
    QCOMPARE(last.serverMessageId, 201LL);
}

void DatabaseManagerTest::updateMessage()
{
    Message updated(
        QStringLiteral("Updated content"), 4000, QStringLiteral("me"), true,
        Message::DeliveryStatus::Read, QStringLiteral("client-1"), 102);
    m_db->updateMessage(QStringLiteral("c-msg"), 102, updated);

    const QList<Message> loaded = m_db->loadMessages(QStringLiteral("c-msg"));
    bool found = false;
    for (const Message &msg : loaded) {
        if (msg.serverMessageId == 102) {
            QCOMPARE(msg.content, QStringLiteral("Updated content"));
            QCOMPARE(msg.status, Message::DeliveryStatus::Read);
            found = true;
            break;
        }
    }
    QVERIFY(found);
}

void DatabaseManagerTest::loadMessagesRespectsLimit()
{
    const QList<Message> loaded = m_db->loadMessages(QStringLiteral("c-msg"), 2);
    QVERIFY(loaded.size() <= 2);
}

void DatabaseManagerTest::trimOldMessages()
{
    QList<Message> manyMessages;
    for (int i = 0; i < 10; ++i) {
        manyMessages.append(Message(
            QStringLiteral("msg %1").arg(i), 5000 + i, QStringLiteral("user"), false,
            Message::DeliveryStatus::Sent, QString(), 300 + i));
    }
    m_db->saveMessages(QStringLiteral("c-trim"), manyMessages);

    const QList<Message> before = m_db->loadMessages(QStringLiteral("c-trim"), 100);
    QCOMPARE(before.size(), 10);

    m_db->trimOldMessages(3);

    const QList<Message> after = m_db->loadMessages(QStringLiteral("c-trim"), 100);
    QVERIFY(after.size() < before.size());
    QVERIFY(after.size() >= 1);
}

QTEST_MAIN(DatabaseManagerTest)
#include "databasemanager_test.moc"
