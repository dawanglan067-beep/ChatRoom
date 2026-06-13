#include <QtTest>
#include <QRegularExpression>
#include <QTime>

#include "chatstore.h"
#include "conversationlistmodel.h"
#include "messagelistmodel.h"
#include "timeformatutils.h"

class ChatStoreModelTest : public QObject
{
    Q_OBJECT

private slots:
    void conversationPreviewFallsBackToBackendLastMessage();
    void conversationPreviewShowsSelfDeliveryStatus();
    void conversationPreviewNormalizesSystemAndMultilineText();
    void conversationPreviewNormalizesMediaMessageVariants();
    void conversationPreviewPrioritizesDraftText();
    void conversationTimeLabelDifferentiatesRecentDays();
    void messageListFormattedTimeDifferentiatesRecentDays();
    void sharedTimeFormatterKeepsListAndMessageConsistent();
    void sharedTimeFormatterHandlesBoundaryDays();
    void sharedTimeFormatterHandlesCrossYearAndInvalid();
    void prependMessagesDeduplicatesByServerMessageId();
    void reorderDoesNotEmitCurrentConversationChangedForOtherConversation();
    void queuedMessageCanTransitionBackToSending();
    void removeQueuedMessageRemovesOnlyQueuedTarget();
    void peerReadReceiptMarksSelfMessagesAsRead();
    void markConversationReadByIdClearsUnreadBadge();
    void recalledMessageUpdatesConversationPreview();
};

void ChatStoreModelTest::conversationPreviewFallsBackToBackendLastMessage()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    conversation.lastMessagePreview = QStringLiteral("backend-preview");
    conversation.lastMessageTimestamp = QDateTime::currentMSecsSinceEpoch();

    store.replaceConversations({conversation});

    const QModelIndex row0 = model.index(0, 0);
    QCOMPARE(row0.data(ConversationListModel::LastMessageRole).toString(),
             QStringLiteral("backend-preview"));
    QVERIFY(!row0.data(ConversationListModel::LastMessageTimeRole).toString().isEmpty());

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("loaded-message"), QDateTime::currentMSecsSinceEpoch(),
                QStringLiteral("tester"), false, Message::DeliveryStatus::Sent, QString(), 101));

    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));
    QCOMPARE(row0.data(ConversationListModel::LastMessageRole).toString(),
             QStringLiteral("loaded-message"));
}

void ChatStoreModelTest::conversationPreviewShowsSelfDeliveryStatus()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    store.replaceConversations({conversation});

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("hello"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("me"),
                true, Message::DeliveryStatus::Read, QStringLiteral("client-1"), 301));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));

    const QModelIndex row0 = model.index(0, 0);
    const QString preview = row0.data(ConversationListModel::LastMessageRole).toString();
    QVERIFY(preview.contains(QStringLiteral("\u4f60:")));
    QVERIFY(preview.contains(QStringLiteral("\u5df2\u8bfb")));
}

void ChatStoreModelTest::conversationPreviewNormalizesSystemAndMultilineText()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    store.replaceConversations({conversation});

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("[\u7cfb\u7edf] line1\nline2"), QDateTime::currentMSecsSinceEpoch(),
                QStringLiteral("system"), false, Message::DeliveryStatus::Sent, QString(), 302));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));

    const QModelIndex row0 = model.index(0, 0);
    const QString preview = row0.data(ConversationListModel::LastMessageRole).toString();
    QVERIFY(!preview.contains(QStringLiteral("\n")));
    QVERIFY(preview.startsWith(QStringLiteral("\u7cfb\u7edf\u6d88\u606f:")));
}

void ChatStoreModelTest::conversationPreviewNormalizesMediaMessageVariants()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    store.replaceConversations({conversation});

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("[Image] avatar.png\n/uploads/abc.png"),
                QDateTime::currentMSecsSinceEpoch(),
                QStringLiteral("peer"),
                false,
                Message::DeliveryStatus::Sent,
                QString(),
                333));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));

    const QModelIndex row0 = model.index(0, 0);
    QCOMPARE(row0.data(ConversationListModel::LastMessageRole).toString(),
             QStringLiteral("[图片] avatar.png"));

    loadedMessages.clear();
    loadedMessages.append(
        Message(QStringLiteral("[File] report.pdf\nURL: /uploads/report.pdf"),
                QDateTime::currentMSecsSinceEpoch(),
                QStringLiteral("me"),
                true,
                Message::DeliveryStatus::Sent,
                QStringLiteral("client-333"),
                334));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));
    const QString selfPreview = row0.data(ConversationListModel::LastMessageRole).toString();
    QVERIFY(selfPreview.startsWith(QStringLiteral("你: [文件] report.pdf")));
}

void ChatStoreModelTest::conversationPreviewPrioritizesDraftText()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    conversation.lastMessagePreview = QStringLiteral("server-last");
    conversation.lastMessageTimestamp = QDateTime::currentMSecsSinceEpoch();
    store.replaceConversations({conversation});

    QVERIFY(store.setConversationDraft(QStringLiteral("c-1"), QStringLiteral("draft text")));
    const QModelIndex row0 = model.index(0, 0);
    const QString preview = row0.data(ConversationListModel::LastMessageRole).toString();
    const QString timeText = row0.data(ConversationListModel::LastMessageTimeRole).toString();
    QVERIFY(preview.startsWith(QStringLiteral("\u3010\u8349\u7a3f\u3011")));
    QVERIFY(timeText.contains(QStringLiteral("\u8349\u7a3f")));

    QVERIFY(store.setConversationDraft(QStringLiteral("c-1"), QStringLiteral("")));
    QCOMPARE(row0.data(ConversationListModel::LastMessageRole).toString(),
             QStringLiteral("server-last"));
}

void ChatStoreModelTest::conversationTimeLabelDifferentiatesRecentDays()
{
    ChatStore store;
    ConversationListModel model(&store);

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    conversation.lastMessagePreview = QStringLiteral("preview");
    conversation.lastMessageTimestamp = now;
    store.replaceConversations({conversation});

    const QModelIndex row0 = model.index(0, 0);
    const QString todayTime = row0.data(ConversationListModel::LastMessageTimeRole).toString();
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{2}:\\d{2}$")).match(todayTime).hasMatch());

    Conversation yesterdayConversation = store.conversations().at(0);
    yesterdayConversation.lastMessageTimestamp = now - 24LL * 60LL * 60LL * 1000LL;
    store.replaceConversations({yesterdayConversation});
    const QString yesterdayTime = model.index(0, 0).data(ConversationListModel::LastMessageTimeRole).toString();
    QVERIFY(yesterdayTime.startsWith(QStringLiteral("昨天 ")));

    Conversation thisWeekConversation = store.conversations().at(0);
    thisWeekConversation.lastMessageTimestamp = now - 3LL * 24LL * 60LL * 60LL * 1000LL;
    store.replaceConversations({thisWeekConversation});
    const QString weekdayTime = model.index(0, 0).data(ConversationListModel::LastMessageTimeRole).toString();
    QVERIFY(QRegularExpression(QStringLiteral("^周[一二三四五六日]$")).match(weekdayTime).hasMatch());
}

void ChatStoreModelTest::messageListFormattedTimeDifferentiatesRecentDays()
{
    ChatStore store;
    MessageListModel model(&store);
    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    store.replaceConversations({conversation});

    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("today"), now, QStringLiteral("peer"),
                false, Message::DeliveryStatus::Sent, QString(), 501));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));
    const QString todayTime = model.index(0, 0).data(MessageListModel::FormattedTimeRole).toString();
    QVERIFY(todayTime.startsWith(QStringLiteral("今天 ")));

    loadedMessages.clear();
    loadedMessages.append(
        Message(QStringLiteral("yesterday"), now - 24LL * 60LL * 60LL * 1000LL, QStringLiteral("peer"),
                false, Message::DeliveryStatus::Sent, QString(), 502));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));
    const QString yesterdayTime = model.index(0, 0).data(MessageListModel::FormattedTimeRole).toString();
    QVERIFY(yesterdayTime.startsWith(QStringLiteral("昨天 ")));

    loadedMessages.clear();
    loadedMessages.append(
        Message(QStringLiteral("weekday"), now - 3LL * 24LL * 60LL * 60LL * 1000LL, QStringLiteral("peer"),
                false, Message::DeliveryStatus::Sent, QString(), 503));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));
    const QString weekdayTime = model.index(0, 0).data(MessageListModel::FormattedTimeRole).toString();
    QVERIFY(QRegularExpression(QStringLiteral("^周[一二三四五六日] \\d{2}:\\d{2}$")).match(weekdayTime).hasMatch());
}

void ChatStoreModelTest::sharedTimeFormatterKeepsListAndMessageConsistent()
{
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 threeDaysAgo = now - 3LL * 24LL * 60LL * 60LL * 1000LL;

    const QString conversationLabel = formatConversationTimeLabel(threeDaysAgo);
    const QString messageLabel = formatMessageTimeLabel(threeDaysAgo);

    QVERIFY(QRegularExpression(QStringLiteral("^周[一二三四五六日]$")).match(conversationLabel).hasMatch());
    QVERIFY(messageLabel.startsWith(conversationLabel + QStringLiteral(" ")));
}

void ChatStoreModelTest::sharedTimeFormatterHandlesBoundaryDays()
{
    const QDate today = QDate::currentDate();
    const qint64 sixDaysAgo = QDateTime(today.addDays(-6), QTime(12, 34)).toMSecsSinceEpoch();
    const qint64 sevenDaysAgo = QDateTime(today.addDays(-7), QTime(12, 34)).toMSecsSinceEpoch();

    const QString sixDayConversation = formatConversationTimeLabel(sixDaysAgo);
    const QString sixDayMessage = formatMessageTimeLabel(sixDaysAgo);
    QVERIFY(sixDayConversation.startsWith(QStringLiteral("周")));
    QVERIFY(sixDayMessage.startsWith(sixDayConversation + QStringLiteral(" ")));

    const QString sevenDayConversation = formatConversationTimeLabel(sevenDaysAgo);
    const QString sevenDayMessage = formatMessageTimeLabel(sevenDaysAgo);
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{2}-\\d{2}$")).match(sevenDayConversation).hasMatch());
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{2}-\\d{2} \\d{2}:\\d{2}$"))
                .match(sevenDayMessage)
                .hasMatch());
}

void ChatStoreModelTest::sharedTimeFormatterHandlesCrossYearAndInvalid()
{
    QVERIFY(formatConversationTimeLabel(0).isEmpty());
    QVERIFY(formatMessageTimeLabel(0).isEmpty());
    QVERIFY(formatAbsoluteDateTimeLabel(0).isEmpty());

    const QDate currentDate = QDate::currentDate();
    const qint64 crossYearMs =
        QDateTime(QDate(currentDate.year() - 1, 12, 31), QTime(23, 59)).toMSecsSinceEpoch();

    const QString crossYearConversation = formatConversationTimeLabel(crossYearMs);
    const QString crossYearMessage = formatMessageTimeLabel(crossYearMs);
    const QString absoluteLabel = formatAbsoluteDateTimeLabel(crossYearMs);

    QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2}$"))
                .match(crossYearConversation)
                .hasMatch());
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}$"))
                .match(crossYearMessage)
                .hasMatch());
    QVERIFY(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"))
                .match(absoluteLabel)
                .hasMatch());
}

void ChatStoreModelTest::prependMessagesDeduplicatesByServerMessageId()
{
    ChatStore store;

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("test-conversation"), {});
    store.replaceConversations({conversation});

    QList<Message> currentMessages;
    currentMessages.append(
        Message(QStringLiteral("m100"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("u"),
                false, Message::DeliveryStatus::Sent, QString(), 100));
    currentMessages.append(
        Message(QStringLiteral("m101"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("u"),
                false, Message::DeliveryStatus::Sent, QString(), 101));
    QVERIFY(store.replaceMessagesForConversation(0, currentMessages));

    QList<Message> olderMessages;
    olderMessages.append(
        Message(QStringLiteral("m099"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("u"),
                false, Message::DeliveryStatus::Sent, QString(), 99));
    olderMessages.append(
        Message(QStringLiteral("m100-dup"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("u"),
                false, Message::DeliveryStatus::Sent, QString(), 100));

    QVERIFY(store.prependMessagesForConversation(0, olderMessages));
    const Conversation *updatedConversation = store.conversationAt(0);
    QVERIFY(updatedConversation != nullptr);
    QCOMPARE(updatedConversation->messages.size(), 3);
    QCOMPARE(updatedConversation->messages.at(0).serverMessageId, 99);
    QCOMPARE(updatedConversation->messages.at(1).serverMessageId, 100);
    QCOMPARE(updatedConversation->messages.at(2).serverMessageId, 101);

    QList<Message> duplicateOnly;
    duplicateOnly.append(
        Message(QStringLiteral("m099-dup"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("u"),
                false, Message::DeliveryStatus::Sent, QString(), 99));
    QVERIFY(!store.prependMessagesForConversation(0, duplicateOnly));
    QCOMPARE(updatedConversation->messages.size(), 3);
}

void ChatStoreModelTest::reorderDoesNotEmitCurrentConversationChangedForOtherConversation()
{
    ChatStore store;
    ConversationListModel model(&store);
    Q_UNUSED(model);

    Conversation conversation1(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    Conversation conversation2(QStringLiteral("c-2"), QStringLiteral("conversation-2"), {});
    store.replaceConversations({conversation1, conversation2});

    QCOMPARE(store.currentConversationIndex(), 0);
    QVERIFY(store.currentConversation() != nullptr);
    QCOMPARE(store.currentConversation()->id, QStringLiteral("c-1"));

    QSignalSpy currentConversationChangedSpy(&store, &ChatStore::currentConversationChanged);

    const Message incoming(QStringLiteral("other conversation message"),
                           QDateTime::currentMSecsSinceEpoch(),
                           QStringLiteral("peer"),
                           false,
                           Message::DeliveryStatus::Sent,
                           QString(),
                           5001);

    QVERIFY(store.appendMessageToConversation(QStringLiteral("c-2"), incoming));

    QVERIFY(store.currentConversation() != nullptr);
    QCOMPARE(store.currentConversation()->id, QStringLiteral("c-1"));
    QCOMPARE(currentConversationChangedSpy.count(), 0);
}

void ChatStoreModelTest::queuedMessageCanTransitionBackToSending()
{
    ChatStore store;
    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    store.replaceConversations({conversation});

    store.setCurrentConversation(0);
    const QString clientMessageId = QStringLiteral("client-queued-001");
    QVERIFY(store.addPendingMessageToCurrentChat(QStringLiteral("queued-message"), clientMessageId));

    QVERIFY(store.markMessageQueued(QStringLiteral("c-1"), clientMessageId));
    const Conversation *afterQueued = store.conversationAt(0);
    QVERIFY(afterQueued != nullptr);
    QCOMPARE(afterQueued->messages.size(), 1);
    QCOMPARE(afterQueued->messages.at(0).status, Message::DeliveryStatus::Queued);

    QVERIFY(store.markMessageSending(QStringLiteral("c-1"), clientMessageId));
    const Conversation *afterSending = store.conversationAt(0);
    QVERIFY(afterSending != nullptr);
    QCOMPARE(afterSending->messages.at(0).status, Message::DeliveryStatus::Sending);
}

void ChatStoreModelTest::removeQueuedMessageRemovesOnlyQueuedTarget()
{
    ChatStore store;
    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    store.replaceConversations({conversation});
    store.setCurrentConversation(0);

    const QString queuedId = QStringLiteral("client-queued-001");
    const QString sendingId = QStringLiteral("client-sending-002");

    QVERIFY(store.addPendingMessageToCurrentChat(QStringLiteral("queued"), queuedId));
    QVERIFY(store.markMessageQueued(QStringLiteral("c-1"), queuedId));
    QVERIFY(store.addPendingMessageToCurrentChat(QStringLiteral("sending"), sendingId));

    const Conversation *beforeRemove = store.conversationAt(0);
    QVERIFY(beforeRemove != nullptr);
    QCOMPARE(beforeRemove->messages.size(), 2);

    QVERIFY(!store.removeQueuedMessage(QStringLiteral("c-1"), sendingId));
    QVERIFY(store.removeQueuedMessage(QStringLiteral("c-1"), queuedId));

    const Conversation *afterRemove = store.conversationAt(0);
    QVERIFY(afterRemove != nullptr);
    QCOMPARE(afterRemove->messages.size(), 1);
    QCOMPARE(afterRemove->messages.at(0).clientMessageId, sendingId);
}

void ChatStoreModelTest::peerReadReceiptMarksSelfMessagesAsRead()
{
    ChatStore store;
    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    store.replaceConversations({conversation});

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("peer-1"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("peer"),
                false, Message::DeliveryStatus::Sent, QString(), 201));
    loadedMessages.append(
        Message(QStringLiteral("self-1"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("me"),
                true, Message::DeliveryStatus::Delivered, QStringLiteral("c1"), 202));
    loadedMessages.append(
        Message(QStringLiteral("self-2"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("me"),
                true, Message::DeliveryStatus::Delivered, QStringLiteral("c2"), 203));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));

    QVERIFY(store.markMessagesReadByPeer(QStringLiteral("c-1"), 202));

    const Conversation *updated = store.conversationAt(0);
    QVERIFY(updated != nullptr);
    QCOMPARE(updated->messages.at(0).status, Message::DeliveryStatus::Sent);
    QCOMPARE(updated->messages.at(1).status, Message::DeliveryStatus::Read);
    QCOMPARE(updated->messages.at(2).status, Message::DeliveryStatus::Delivered);

    QVERIFY(!store.markMessagesReadByPeer(QStringLiteral("c-1"), 202));
}

void ChatStoreModelTest::markConversationReadByIdClearsUnreadBadge()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation1(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    Conversation conversation2(QStringLiteral("c-2"), QStringLiteral("conversation-2"), {});
    conversation2.unreadCount = 3;
    store.replaceConversations({conversation1, conversation2});

    const QModelIndex row1 = model.index(1, 0);
    QCOMPARE(row1.data(ConversationListModel::UnreadCountRole).toInt(), 3);

    QVERIFY(store.markConversationReadById(QStringLiteral("c-2")));
    QCOMPARE(row1.data(ConversationListModel::UnreadCountRole).toInt(), 0);
    QVERIFY(!store.markConversationReadById(QStringLiteral("c-2")));
    QVERIFY(!store.markConversationReadById(QStringLiteral("missing")));
}

void ChatStoreModelTest::recalledMessageUpdatesConversationPreview()
{
    ChatStore store;
    ConversationListModel model(&store);

    Conversation conversation(QStringLiteral("c-1"), QStringLiteral("conversation-1"), {});
    store.replaceConversations({conversation});

    QList<Message> loadedMessages;
    loadedMessages.append(
        Message(QStringLiteral("hello"), QDateTime::currentMSecsSinceEpoch(), QStringLiteral("me"),
                true, Message::DeliveryStatus::Delivered, QStringLiteral("client-1"), 401));
    QVERIFY(store.replaceMessagesForConversation(0, loadedMessages));

    const Message recalledMessage(
        QStringLiteral("[系统] 一条消息已撤回"),
        QDateTime::currentMSecsSinceEpoch(),
        QStringLiteral("system"),
        false,
        Message::DeliveryStatus::Sent,
        QString(),
        401);
    QVERIFY(store.markMessageRecalled(QStringLiteral("c-1"), 401, recalledMessage));

    const Conversation *updatedConversation = store.conversationAt(0);
    QVERIFY(updatedConversation != nullptr);
    QCOMPARE(updatedConversation->messages.size(), 1);
    QCOMPARE(updatedConversation->messages.at(0).isSelf, false);
    QCOMPARE(updatedConversation->messages.at(0).senderId, QStringLiteral("system"));
    QCOMPARE(updatedConversation->messages.at(0).content,
             QStringLiteral("[系统] 一条消息已撤回"));

    const QModelIndex row0 = model.index(0, 0);
    const QString preview = row0.data(ConversationListModel::LastMessageRole).toString();
    QVERIFY(preview.startsWith(QStringLiteral("系统消息:")));
}

QTEST_MAIN(ChatStoreModelTest)
#include "chatstoremodel_test.moc"
