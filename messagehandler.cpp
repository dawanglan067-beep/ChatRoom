#include "messagehandler.h"
#include "chatclient.h"
#include "chatstore.h"
#include "chatutils.h"
#include "conversation.h"
#include "mediautils.h"
#include "message.h"
#include "networkservice.h"
#include "uitexts.h"

#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHttpMultiPart>
#include <QHttpPart>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMimeDatabase>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <QUrl>

namespace
{
}

MessageHandler::MessageHandler(ChatStore *chatStore, ChatClient *chatClient, NetworkService *networkService, QObject *parent)
    : QObject(parent)
    , m_chatStore(chatStore)
    , m_chatClient(chatClient)
    , m_networkService(networkService)
{
}

MessageHandler::~MessageHandler() = default;

void MessageHandler::loadMessages(const QString &backendBaseUrl, int index, qint64 beforeId, bool prepend)
{
    const Conversation *conversation = m_chatStore->conversationAt(index);
    if (!conversation) {
        return;
    }

    if (backendBaseUrl.isEmpty()) {
        return;
    }

    const QString conversationId = conversation->id;
    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/messages").arg(conversationId));

    m_networkService->getJsonAsync(url, [this, index, conversationId, prepend](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLoadMessagesFailed, result.message);
            return;
        }

        const QJsonArray items = result.body.value(QStringLiteral("messages")).toArray();
        QList<Message> messages;
        messages.reserve(items.size());
        for (const QJsonValue &value : items) {
            const QJsonObject object = value.toObject();
            messages.append(ChatUtils::messageFromBackendPayload(object, QString()));
        }

        if (prepend) {
            m_chatStore->prependMessagesForConversation(index, messages);
        } else {
            m_chatStore->replaceMessagesForConversation(index, messages);
        }

        emit scrollMessagesToBottom();
    });
}

void MessageHandler::sendMessage(const QString &text, const QString &conversationId)
{
    if (text.trimmed().isEmpty() || conversationId.isEmpty()) {
        return;
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    if (!m_chatStore->addPendingMessageToCurrentChat(text, clientMessageId)) {
        return;
    }

    if (m_chatClient->isConnected()) {
        m_chatClient->sendChatMessage(text, conversationId, clientMessageId);
    } else {
        m_chatStore->markMessageQueued(conversationId, clientMessageId);
        emit networkStatusChanged(UiText::MainWindow::kStatusConnectRealtimeFirst,
                         UiText::MainWindow::kStatusMessageQueuedDetail);
    }
}

void MessageHandler::sendMediaFile(const QString &backendBaseUrl, const QString &filePath, const QString &conversationId, QWidget *parent)
{
    if (backendBaseUrl.isEmpty() || filePath.isEmpty() || conversationId.isEmpty()) {
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        if (parent) {
            QMessageBox::warning(parent, UiText::MessageHandler::kSendFileDialogTitle, UiText::MessageHandler::kCannotOpenFile);
        }
        return;
    }

    const QByteArray bytes = file.readAll();
    constexpr int kMaxUploadBytes = 8 * 1024 * 1024;
    if (bytes.isEmpty()) {
        if (parent) {
            QMessageBox::warning(parent, UiText::MessageHandler::kSendFileDialogTitle, UiText::MessageHandler::kFileEmpty);
        }
        return;
    }
    if (bytes.size() > kMaxUploadBytes) {
        if (parent) {
            QMessageBox::warning(parent, UiText::MessageHandler::kSendFileDialogTitle, UiText::MessageHandler::kFileTooLarge);
        }
        return;
    }

    const QFileInfo info(file);
    const QString fileName = info.fileName().trimmed();
    QMimeDatabase mimeDatabase;
    QString mimeType = mimeDatabase.mimeTypeForFile(info).name().trimmed();
    if (mimeType.isEmpty()) {
        mimeType = QStringLiteral("application/octet-stream");
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/media").arg(conversationId));

    emit networkStatusChanged(UiText::MainWindow::kStatusUploadingFile,
                              QStringLiteral("%1 (%2 KB)").arg(fileName).arg((bytes.size() + 1023) / 1024));

    auto *multipart = new QHttpMultiPart(QHttpMultiPart::FormDataType);
    auto addTextPart = [multipart](const QString &name, const QString &value) {
        QHttpPart part;
        part.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"%1\"").arg(name));
        part.setBody(value.toUtf8());
        multipart->append(part);
    };

    addTextPart(QStringLiteral("fileName"), fileName);
    addTextPart(QStringLiteral("mimeType"), mimeType);
    addTextPart(QStringLiteral("clientMessageId"), clientMessageId);

    QHttpPart filePart;
    filePart.setHeader(QNetworkRequest::ContentDispositionHeader,
                       QStringLiteral("form-data; name=\"file\"; filename=\"%1\"").arg(fileName));
    filePart.setHeader(QNetworkRequest::ContentTypeHeader, mimeType);
    filePart.setBody(bytes);
    multipart->append(filePart);

    QNetworkRequest request(url);
    request.setRawHeader("Accept", "application/json");
    const QString authToken = m_networkService->authToken();
    if (!authToken.isEmpty()) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(authToken).toUtf8());
    }

    QNetworkReply *reply = m_networkService->networkManager()->post(request, multipart);
    multipart->setParent(reply);
    connect(reply, &QNetworkReply::uploadProgress, this, [this, fileName](qint64 sent, qint64 total) {
        if (total <= 0) {
            return;
        }
        const int percentage = static_cast<int>((sent * 100) / total);
        emit networkStatusChanged(UiText::MainWindow::kStatusUploadingFile,
                                  QStringLiteral("%1（%2%）").arg(fileName).arg(percentage));
    });

    NetworkService::setupReplyTimeout(reply, 30000);

    connect(reply, &QNetworkReply::finished, this, [this, reply, conversationId]() {
        const NetworkService::HttpResult result = NetworkService::parseReplyResult(reply);
        reply->deleteLater();

        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusSendFileFailed, result.message);
            return;
        }

        if (!m_chatClient->isConnected()) {
            const QJsonObject messageObject = result.body.value(QStringLiteral("message")).toObject();
            if (!messageObject.isEmpty()) {
                const Message message = ChatUtils::messageFromBackendPayload(messageObject, QString());
                m_chatStore->appendMessageToConversation(conversationId, message);
                emit scrollMessagesToBottom();
            }
        }

        const QJsonObject fileObject = result.body.value(QStringLiteral("file")).toObject();
        emit networkStatusChanged(UiText::MainWindow::kStatusFileSent,
                                  fileObject.value(QStringLiteral("name")).toString());
    });
}

void MessageHandler::recallMessage(const QString &conversationId, qint64 messageId)
{
    if (conversationId.isEmpty() || messageId <= 0) {
        return;
    }

    m_chatClient->recallMessage(conversationId, messageId);
}

void MessageHandler::markMessageRead(const QString &backendBaseUrl, const QString &conversationId, qint64 messageId)
{
    if (backendBaseUrl.isEmpty() || conversationId.isEmpty() || messageId <= 0) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/read").arg(conversationId));

    m_networkService->postJsonAsync(url,
                  QJsonObject{{QStringLiteral("messageId"), messageId}},
                  [this, conversationId](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          return;
                      }

                      m_chatStore->markConversationReadById(conversationId);
                  });
}

void MessageHandler::handleRawMessageReceived(const QString &messageText)
{
    Q_UNUSED(messageText);
}

void MessageHandler::handleJsonMessageReceived(const QJsonObject &payload)
{
    const QString type = payload.value(QStringLiteral("type")).toString();

    if (type == QStringLiteral("connected")) {
        emit networkStatusChanged(UiText::MainWindow::kStatusRealtimeConnected);
        emit realtimeConnected();
        return;
    }

    if (type == QStringLiteral("conversation_joined")) {
        emit networkStatusChanged(UiText::MainWindow::kStatusJoinedConversation,
                                  payload.value(QStringLiteral("conversationId")).toString());
        return;
    }

    if (type == QStringLiteral("message_created")) {
        const QString conversationId = payload.value(QStringLiteral("conversationId")).toString();
        const QJsonObject messageObject = payload.value(QStringLiteral("message")).toObject();
        const Message message = ChatUtils::messageFromBackendPayload(messageObject, QString());
        m_chatStore->appendMessageToConversation(conversationId, message);
        emit messageReceived(conversationId, message);
        return;
    }

    if (type == QStringLiteral("message_recalled")) {
        const QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
        const qint64 messageId = payload.value(QStringLiteral("messageId")).toInteger();
        processMessageRecalled(payload, QString());
        emit messageRecalled(conversationId, messageId);
        return;
    }

    if (type == QStringLiteral("conversation_read")) {
        processConversationRead(payload, QString());
        return;
    }

    if (type == QStringLiteral("profile_updated")) {
        const QJsonObject user = payload.value(QStringLiteral("user")).toObject();
        const QString email = user.value(QStringLiteral("email")).toString().trimmed();
        const QString nickname = user.value(QStringLiteral("nickname")).toString().trimmed();
        const QString avatarUrl = user.value(QStringLiteral("avatarUrl")).toString().trimmed();
        if (!email.isEmpty() && !nickname.isEmpty()) {
            emit profileUpdatedFromServer(email, nickname, avatarUrl);
        }
        return;
    }

    if (type == QStringLiteral("typing_state")) {
        processTypingState(payload, QString());
        return;
    }

    if (type == QStringLiteral("presence_state")) {
        processPresenceState(payload);
        return;
    }

    if (type == QStringLiteral("error")) {
        const QString clientMessageId = payload.value(QStringLiteral("clientMessageId")).toString();
        if (!clientMessageId.isEmpty()) {
            QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
            if (conversationId.isEmpty()) {
                conversationId = m_pendingMessageConversationIds.value(clientMessageId);
            }
            m_chatStore->markMessageFailed(conversationId, clientMessageId);
            clearPendingMessageTimeout(clientMessageId);
            m_pendingMessageConversationIds.remove(clientMessageId);
        }
        emit serverError(clientMessageId,
                         payload.value(QStringLiteral("conversationId")).toString(),
                         payload.value(QStringLiteral("message")).toString());
        return;
    }

    emit networkStatusChanged(UiText::MainWindow::kStatusRealtimeEvent,
                              QString::fromUtf8(QJsonDocument(payload).toJson(QJsonDocument::Compact)));
}

void MessageHandler::saveDraft(const QString &conversationId, const QString &draftText)
{
    if (conversationId.isEmpty()) {
        return;
    }

    m_conversationDrafts.insert(conversationId, draftText);
}

void MessageHandler::restoreDraft(const QString &conversationId)
{
    if (conversationId.isEmpty()) {
        return;
    }

    const QString draft = m_conversationDrafts.value(conversationId);
    emit networkStatusChanged(QStringLiteral("draft_restored"), draft);
}

void MessageHandler::loadDraftsFromSettings()
{
    QSettings settings;
    const QStringList keys = settings.childGroups();
    for (const QString &key : keys) {
        if (key.startsWith(QStringLiteral("drafts/"))) {
            const QString conversationId = key.mid(7);
            const QString draft = settings.value(key + QStringLiteral("/text")).toString();
            if (!draft.isEmpty()) {
                m_conversationDrafts.insert(conversationId, draft);
            }
        }
    }
}

void MessageHandler::persistDraftsToSettings() const
{
    QSettings settings;
    for (auto it = m_conversationDrafts.constBegin(); it != m_conversationDrafts.constEnd(); ++it) {
        const QString key = QStringLiteral("drafts/") + it.key();
        settings.setValue(key + QStringLiteral("/text"), it.value());
    }
    settings.sync();
}

void MessageHandler::toggleFavorite(const QString &conversationId, qint64 messageId)
{
    if (conversationId.isEmpty() || messageId <= 0) {
        return;
    }

    const QString key = QStringLiteral("%1#%2").arg(conversationId).arg(messageId);
    if (m_favoriteMessagesByKey.contains(key)) {
        m_favoriteMessagesByKey.remove(key);
    } else {
        m_favoriteMessagesByKey.insert(key, QJsonObject{
            {QStringLiteral("conversationId"), conversationId},
            {QStringLiteral("messageId"), messageId}
        });
    }
}

void MessageHandler::loadFavoritesFromSettings()
{
    QSettings settings;
    const QStringList keys = settings.childGroups();
    for (const QString &key : keys) {
        if (key.startsWith(QStringLiteral("favorites/"))) {
            const QString favoriteKey = key.mid(10);
            const QJsonObject favorite = QJsonObject{
                {QStringLiteral("conversationId"), settings.value(key + QStringLiteral("/conversationId")).toString()},
                {QStringLiteral("messageId"), settings.value(key + QStringLiteral("/messageId")).toLongLong()}
            };
            m_favoriteMessagesByKey.insert(favoriteKey, favorite);
        }
    }
}

void MessageHandler::persistFavoritesToSettings() const
{
    QSettings settings;
    for (auto it = m_favoriteMessagesByKey.constBegin(); it != m_favoriteMessagesByKey.constEnd(); ++it) {
        const QString key = QStringLiteral("favorites/") + it.key();
        settings.setValue(key + QStringLiteral("/conversationId"), it.value().value(QStringLiteral("conversationId")).toString());
        settings.setValue(key + QStringLiteral("/messageId"), it.value().value(QStringLiteral("messageId")).toInteger());
    }
    settings.sync();
}

bool MessageHandler::isFavoriteMessage(const QString &conversationId, qint64 messageId) const
{
    if (conversationId.isEmpty() || messageId <= 0) {
        return false;
    }

    const QString key = QStringLiteral("%1#%2").arg(conversationId).arg(messageId);
    return m_favoriteMessagesByKey.contains(key);
}

void MessageHandler::searchMessages(const QString &backendBaseUrl, const QString &keyword)
{
    if (backendBaseUrl.isEmpty() || keyword.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/search/messages"));

    m_networkService->getJsonAsync(url, [this, keyword](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusGlobalSearchFailed, result.message);
            return;
        }

        const QJsonArray items = result.body.value(QStringLiteral("results")).toArray();
        if (items.isEmpty()) {
            emit networkStatusChanged(QStringLiteral("search_empty"), keyword);
            return;
        }

        emit searchResultsReady(result.body);
    });
}

void MessageHandler::editMessage(const QString &conversationId, qint64 messageId, const QString &newContent)
{
    if (conversationId.isEmpty() || messageId <= 0 || newContent.isEmpty()) {
        return;
    }

    Q_UNIMPLEMENTED();
    emit networkStatusChanged(UiText::MessageHandler::kEditNotSupported);
}

void MessageHandler::focusMessageByServerIdInConversation(const QString &conversationId, qint64 serverMessageId)
{
    if (conversationId.trimmed().isEmpty() || serverMessageId <= 0) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversation->id != conversationId) {
        return;
    }

    for (int row = 0; row < conversation->messages.size(); ++row) {
        if (conversation->messages.at(row).serverMessageId == serverMessageId) {
            emit focusMessageRequested(conversationId, serverMessageId);
            return;
        }
    }
}

void MessageHandler::refreshFavoriteHighlights()
{
    emit favoriteHighlightsRefreshed();
}

void MessageHandler::appendSystemMessage(const QString &text)
{
    if (text.isEmpty()) {
        return;
    }

    m_chatStore->addReceivedMessageToCurrentChat(
        UiText::MainWindow::kSystemMessagePattern.arg(text),
        QStringLiteral("system"));
    emit scrollMessagesToBottom();
}

void MessageHandler::joinCurrentRoomIfConnected()
{
    if (!m_chatClient->isConnected()) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (conversation && !conversation->id.trimmed().isEmpty()) {
        m_chatClient->joinConversation(conversation->id);
    }
}

void MessageHandler::restartPendingMessageTimeout(const QString &conversationId, const QString &clientMessageId)
{
    if (conversationId.isEmpty() || clientMessageId.isEmpty()) {
        return;
    }

    clearPendingMessageTimeout(clientMessageId);

    auto *timeoutTimer = new QTimer(this);
    timeoutTimer->setSingleShot(true);
    connect(timeoutTimer, &QTimer::timeout, this, [this, conversationId, clientMessageId]() {
        if (m_chatStore->markMessageFailed(conversationId, clientMessageId)) {
            emit networkStatusChanged(UiText::MainWindow::kStatusMessageSendFailed,
                             UiText::MainWindow::kStatusMessageSendFailedDetail);
        }

        clearPendingMessageTimeout(clientMessageId);
        m_pendingMessageConversationIds.remove(clientMessageId);
    });
    timeoutTimer->start(10000);

    m_pendingMessageTimers.insert(clientMessageId, timeoutTimer);
    m_pendingMessageConversationIds.insert(clientMessageId, conversationId);
}

void MessageHandler::clearPendingMessageTimeout(const QString &clientMessageId)
{
    if (clientMessageId.isEmpty()) {
        return;
    }

    auto it = m_pendingMessageTimers.find(clientMessageId);
    if (it != m_pendingMessageTimers.end()) {
        it.value()->stop();
        it.value()->deleteLater();
        m_pendingMessageTimers.erase(it);
    }
}

void MessageHandler::resumeQueuedMessagesAfterReconnect()
{
    const QStringList pendingIds = m_pendingMessageConversationIds.keys();
    for (const QString &clientMessageId : pendingIds) {
        const QString conversationId = m_pendingMessageConversationIds.value(clientMessageId);
        if (conversationId.isEmpty()) {
            continue;
        }

        if (!m_chatStore->markMessageSending(conversationId, clientMessageId)) {
            continue;
        }

        restartPendingMessageTimeout(conversationId, clientMessageId);
    }
}

Message MessageHandler::processMessageFromBackend(const QJsonObject &object, const QString &loggedInEmail)
{
    return ChatUtils::messageFromBackendPayload(object, loggedInEmail);
}

void MessageHandler::processMessageCreated(const QJsonObject &payload, const QString &loggedInEmail)
{
    const QString conversationId = payload.value(QStringLiteral("conversationId")).toString();
    const QString clientMessageId = payload.value(QStringLiteral("clientMessageId")).toString();
    const QJsonObject messageObject = payload.value(QStringLiteral("message")).toObject();
    if (messageObject.isEmpty()) {
        return;
    }

    const QString senderEmail = messageObject.value(QStringLiteral("senderEmail")).toString().trimmed().toLower();
    if (!senderEmail.isEmpty()) {
        auto typingUsers = m_typingUsersByConversationId.value(conversationId);
        typingUsers.remove(senderEmail);
        if (typingUsers.isEmpty()) {
            m_typingUsersByConversationId.remove(conversationId);
        } else {
            m_typingUsersByConversationId.insert(conversationId, typingUsers);
        }
        emit typingUsersUpdated(conversationId, typingUsers);
    }

    const Message message = ChatUtils::messageFromBackendPayload(messageObject, loggedInEmail);
    clearPendingMessageTimeout(clientMessageId);
    m_pendingMessageConversationIds.remove(clientMessageId);

    const bool handled = message.isSelf
        ? m_chatStore->markPendingMessageSent(conversationId, clientMessageId, message)
        : m_chatStore->appendMessageToConversation(conversationId, message);

    if (handled) {
        if (message.serverMessageId > 0) {
            markMessageRead(m_networkService->backendBaseUrl(), conversationId, message.serverMessageId);
        }
        emit messageReceived(conversationId, message);
    }
}

void MessageHandler::processMessageRecalled(const QJsonObject &payload, const QString &loggedInEmail)
{
    const QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
    const qint64 messageId = payload.value(QStringLiteral("messageId")).toInteger(0);
    const QJsonObject messageObject = payload.value(QStringLiteral("message")).toObject();
    if (conversationId.isEmpty() || messageId <= 0 || messageObject.isEmpty()) {
        return;
    }

    Message recalledMessage = ChatUtils::messageFromBackendPayload(messageObject, loggedInEmail);

    const QString favoriteKey = QStringLiteral("%1#%2").arg(conversationId).arg(messageId);
    if (m_favoriteMessagesByKey.remove(favoriteKey) > 0) {
        persistFavoritesToSettings();
        emit favoriteHighlightsRefreshed();
    }

    const QString recalledByEmail = payload.value(QStringLiteral("recalledByEmail")).toString().trimmed();
    const QString recalledByNickname = payload.value(QStringLiteral("recalledByNickname")).toString().trimmed();
    const bool recalledBySelf = recalledByEmail.compare(loggedInEmail, Qt::CaseInsensitive) == 0;
    const QString recallDetail = recalledBySelf
        ? UiText::MainWindow::kRecalledBySelfMessage
        : UiText::MainWindow::kRecalledByUserPattern.arg(recalledByNickname.isEmpty() ? recalledByEmail : recalledByNickname);
    const QString recallText = UiText::MainWindow::kSystemMessagePattern.arg(recallDetail);
    recalledMessage.content = recallText;
    recalledMessage.senderId = UiText::MessageBubble::kSystemSenderKey;
    recalledMessage.isSelf = false;

    m_chatStore->markMessageRecalled(conversationId, messageId, recalledMessage);
    emit messageRecalled(conversationId, messageId);
}

void MessageHandler::processConversationRead(const QJsonObject &payload, const QString &loggedInEmail)
{
    const QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
    const QString readerEmail = payload.value(QStringLiteral("userEmail")).toString().trimmed();
    const qint64 lastReadMessageId = payload.value(QStringLiteral("lastReadMessageId")).toInteger(0);
    if (conversationId.isEmpty() || readerEmail.isEmpty()) {
        return;
    }

    const bool readBySelf = readerEmail.compare(loggedInEmail, Qt::CaseInsensitive) == 0;
    if (readBySelf) {
        m_chatStore->markConversationReadById(conversationId);
        const qint64 currentAck = m_lastReadAckMessageIds.value(conversationId, 0);
        if (lastReadMessageId > currentAck) {
            m_lastReadAckMessageIds.insert(conversationId, lastReadMessageId);
        }
        return;
    }

    m_chatStore->markMessagesReadByPeer(conversationId, lastReadMessageId);
}

void MessageHandler::processTypingState(const QJsonObject &payload, const QString &loggedInEmail)
{
    const QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
    const QString userEmail = payload.value(QStringLiteral("userEmail")).toString().trimmed().toLower();
    const QString userNickname = payload.value(QStringLiteral("userNickname")).toString().trimmed();
    const bool isTyping = payload.value(QStringLiteral("isTyping")).toBool(false);
    if (conversationId.isEmpty() || userEmail.isEmpty()) {
        return;
    }

    if (userEmail.compare(loggedInEmail.trimmed().toLower(), Qt::CaseInsensitive) == 0) {
        return;
    }

    auto typingUsers = m_typingUsersByConversationId.value(conversationId);
    if (isTyping) {
        typingUsers.insert(userEmail, userNickname.isEmpty() ? userEmail : userNickname);
        m_typingUsersByConversationId.insert(conversationId, typingUsers);
    } else {
        typingUsers.remove(userEmail);
        if (typingUsers.isEmpty()) {
            m_typingUsersByConversationId.remove(conversationId);
        } else {
            m_typingUsersByConversationId.insert(conversationId, typingUsers);
        }
    }

    emit typingUsersUpdated(conversationId, typingUsers);
}

void MessageHandler::processPresenceState(const QJsonObject &payload)
{
    const QString conversationId = payload.value(QStringLiteral("conversationId")).toString().trimmed();
    const QString userEmail = payload.value(QStringLiteral("userEmail")).toString().trimmed();
    const bool isOnline = payload.value(QStringLiteral("isOnline")).toBool(false);
    const QString lastSeenAtRaw = payload.value(QStringLiteral("lastSeenAt")).toString().trimmed();
    const qint64 lastSeenAt = lastSeenAtRaw.isEmpty() ? 0 : ChatUtils::parseBackendTimestamp(lastSeenAtRaw);
    if (conversationId.isEmpty() || userEmail.isEmpty()) {
        return;
    }

    emit presenceUpdated(conversationId, userEmail, isOnline, lastSeenAt);
}
