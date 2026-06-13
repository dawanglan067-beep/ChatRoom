#include "chatclient.h"
#include "uitexts.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSettings>
#include <QTimer>
#include <QCryptographicHash>

#ifdef CHATROOM_HAS_WEBSOCKETS
#include <QAbstractSocket>
#include <QUrlQuery>
#include <QWebSocket>
#endif

namespace
{
QString compactJson(const QJsonObject &payload)
{
    const QByteArray utf8 = QJsonDocument(payload).toJson(QJsonDocument::Compact);
    qDebug() << "WebSocket JSON bytes:" << utf8.toHex();
    return QString::fromUtf8(utf8);
}

constexpr int kReconnectInitialDelayMs = 1000;
constexpr int kReconnectMaxDelayMs = 30000;
constexpr int kMaxQueuedMessages = 300;

QString payloadClientMessageId(const QJsonObject &payload)
{
    if (payload.value(QStringLiteral("type")).toString() != QStringLiteral("send_message")) {
        return QString();
    }

    return payload.value(QStringLiteral("clientMessageId")).toString().trimmed();
}

bool queueContainsClientMessageId(const QQueue<QJsonObject> &queue, const QString &clientMessageId)
{
    if (clientMessageId.isEmpty()) {
        return false;
    }

    for (const QJsonObject &queuedPayload : queue) {
        if (payloadClientMessageId(queuedPayload) == clientMessageId) {
            return true;
        }
    }
    return false;
}
}

ChatClient::ChatClient(QObject *parent)
    : QObject(parent)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, [this]() {
        openSocket();
    });

#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = new QWebSocket(QString(), QWebSocketProtocol::VersionLatest, this);
    m_available = true;
    m_socketObject = socket;

    connect(socket, &QWebSocket::connected, this, [this]() {
        m_connected = true;
        m_reconnectAttempt = 0;
        stopReconnect();
        setStatusText(UiText::ChatClient::kConnected);
        flushQueuedMessages();
        emit connected();
    });

    connect(socket, &QWebSocket::disconnected, this, [this]() {
        m_connected = false;
        setStatusText(UiText::ChatClient::kDisconnected);
        if (!m_manualDisconnectRequested) {
            scheduleReconnect();
        }
        emit disconnected();
    });

    connect(socket, &QWebSocket::textMessageReceived, this, [this](const QString &messageText) {
        qDebug() << "WebSocket received:" << messageText;
        emit rawMessageReceived(messageText);

        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(messageText.toUtf8(), &parseError);
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
            emit errorOccurred(UiText::ChatClient::kInvalidJson);
            return;
        }

        emit jsonMessageReceived(document.object());
    });

    connect(socket, &QWebSocket::errorOccurred, this, [this](QAbstractSocket::SocketError) {
        auto *socketObject = qobject_cast<QWebSocket *>(m_socketObject);
        const QString errorText = socketObject ? socketObject->errorString()
                                               : UiText::ChatClient::kUnknownSocketError;
        m_connected = false;
        setStatusText(UiText::ChatClient::kErrorPattern.arg(errorText));
        if (!m_manualDisconnectRequested) {
            scheduleReconnect();
        }
        emit errorOccurred(errorText);
    });

    setStatusText(UiText::ChatClient::kIdle);
#else
    setStatusText(UiText::ChatClient::kWebSocketsUnavailable);
#endif

}

bool ChatClient::isAvailable() const
{
    return m_available;
}

bool ChatClient::isConnected() const
{
    return m_connected;
}

QString ChatClient::statusText() const
{
    return m_statusText;
}

void ChatClient::connectToServer(const QUrl &url, const QString &authToken)
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket) {
        emit errorOccurred(UiText::ChatClient::kSocketNotInitialized);
        return;
    }

    const QString normalizedToken = authToken.trimmed();
    const bool sameEndpoint = (m_serverUrl == url && m_authToken == normalizedToken);
    if (sameEndpoint && (socket->state() == QAbstractSocket::ConnectedState
                         || socket->state() == QAbstractSocket::ConnectingState)) {
        return;
    }

    const QString newQueueStorageKey = makeQueueStorageKey(url, normalizedToken);
    if (m_outboundQueueStorageKey != newQueueStorageKey) {
        if (!m_outboundQueueStorageKey.isEmpty()) {
            QSettings settings(QStringLiteral("ChatRoom"), QStringLiteral("ChatRoomClient"));
            settings.remove(m_outboundQueueStorageKey);
        }
        m_outboundQueue.clear();
        m_outboundQueueStorageKey = newQueueStorageKey;
        restoreOutboundQueue();
    }

    m_authToken = normalizedToken;
    m_serverUrl = url;
    m_manualDisconnectRequested = false;
    m_reconnectAttempt = 0;
    stopReconnect();

    if (socket->state() == QAbstractSocket::ConnectedState || socket->state() == QAbstractSocket::ConnectingState) {
        socket->abort();
    }
    openSocket();
#else
    Q_UNUSED(url);
    Q_UNUSED(authToken);
    emit errorOccurred(UiText::ChatClient::kWebSocketsUnavailable);
#endif
}

void ChatClient::disconnectFromServer()
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket) {
        return;
    }

    m_manualDisconnectRequested = true;
    stopReconnect();
    m_outboundQueue.clear();
    persistOutboundQueue();
    socket->close();
#endif
}

void ChatClient::joinConversation(const QString &conversationId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("join_conversation"));
    payload.insert(QStringLiteral("conversationId"), conversationId);
    sendJson(payload);
}

void ChatClient::sendChatMessage(const QString &text, const QString &conversationId,
                                 const QString &clientMessageId)
{
    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("send_message"));
    payload.insert(QStringLiteral("text"), text);
    payload.insert(QStringLiteral("conversationId"), conversationId);
    if (!clientMessageId.trimmed().isEmpty()) {
        payload.insert(QStringLiteral("clientMessageId"), clientMessageId);
    }
    sendJson(payload);
}

void ChatClient::recallMessage(const QString &conversationId, qint64 messageId)
{
    if (conversationId.trimmed().isEmpty() || messageId <= 0) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("recall_message"));
    payload.insert(QStringLiteral("conversationId"), conversationId);
    payload.insert(QStringLiteral("messageId"), messageId);
    sendJson(payload);
}

void ChatClient::setTypingState(const QString &conversationId, bool isTyping)
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    if (conversationId.trimmed().isEmpty() || !m_connected) {
        return;
    }

    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("type"), QStringLiteral("typing_state"));
    payload.insert(QStringLiteral("conversationId"), conversationId);
    payload.insert(QStringLiteral("isTyping"), isTyping);
    const QString messageText = compactJson(payload);
    qDebug() << "WebSocket sent:" << messageText;
    socket->sendTextMessage(messageText);
#else
    Q_UNUSED(conversationId);
    Q_UNUSED(isTyping);
#endif
}

void ChatClient::sendJson(const QJsonObject &payload)
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket) {
        emit errorOccurred(UiText::ChatClient::kSocketNotInitialized);
        return;
    }

    if (!m_connected) {
        if (m_manualDisconnectRequested || !m_serverUrl.isValid()) {
            emit errorOccurred(UiText::ChatClient::kSocketNotConnected);
            return;
        }

        const QString clientMessageId = payloadClientMessageId(payload);
        if (queueContainsClientMessageId(m_outboundQueue, clientMessageId)) {
            scheduleReconnect();
            setStatusText(UiText::ChatClient::kQueuedReconnecting);
            return;
        }

        if (m_outboundQueue.size() >= kMaxQueuedMessages) {
            m_outboundQueue.dequeue();
        }
        m_outboundQueue.enqueue(payload);
        persistOutboundQueue();
        scheduleReconnect();
        setStatusText(UiText::ChatClient::kQueuedReconnecting);
        return;
    }

    if (socket->state() != QAbstractSocket::ConnectedState) {
        emit errorOccurred(UiText::ChatClient::kSocketNotConnected);
        return;
    }

    const QString messageText = compactJson(payload);
    qDebug() << "WebSocket sent:" << messageText;
    socket->sendTextMessage(messageText);
#else
    Q_UNUSED(payload);
    emit errorOccurred(UiText::ChatClient::kWebSocketsUnavailable);
#endif
}

void ChatClient::setStatusText(const QString &statusText)
{
    if (m_statusText == statusText) {
        return;
    }

    m_statusText = statusText;
    emit connectionStateChanged(m_statusText);
}

void ChatClient::openSocket()
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket || !m_serverUrl.isValid()) {
        return;
    }

    QUrl finalUrl = m_serverUrl;
    if (!m_authToken.isEmpty()) {
        QUrlQuery query(finalUrl);
        query.removeAllQueryItems(QStringLiteral("token"));
        query.addQueryItem(QStringLiteral("token"), m_authToken);
        finalUrl.setQuery(query);
    }

    setStatusText(UiText::ChatClient::kConnectingPattern.arg(finalUrl.toString()));
    socket->open(finalUrl);
#endif
}

void ChatClient::flushQueuedMessages()
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) {
        return;
    }

    while (!m_outboundQueue.isEmpty()) {
        const QJsonObject payload = m_outboundQueue.dequeue();
        const QString messageText = compactJson(payload);
        qDebug() << "WebSocket sent (flushed):" << messageText;
        socket->sendTextMessage(messageText);
    }
    persistOutboundQueue();
#endif
}

QString ChatClient::makeQueueStorageKey(const QUrl &serverUrl, const QString &authToken) const
{
    const QString normalizedServer =
        serverUrl.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::StripTrailingSlash).toString();
    const QByteArray source = (normalizedServer + QStringLiteral("|") + authToken.trimmed()).toUtf8();
    const QByteArray hash = QCryptographicHash::hash(source, QCryptographicHash::Sha256).toHex();
    return QStringLiteral("chatclient/realtime_outbound_queue/%1").arg(QString::fromLatin1(hash.left(24)));
}

void ChatClient::restoreOutboundQueue()
{
    if (m_outboundQueueStorageKey.trimmed().isEmpty()) {
        return;
    }

    QSettings settings(QStringLiteral("ChatRoom"), QStringLiteral("ChatRoomClient"));
    const QByteArray rawValue = settings.value(m_outboundQueueStorageKey).toByteArray();
    if (rawValue.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(rawValue, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isArray()) {
        settings.remove(m_outboundQueueStorageKey);
        return;
    }

    m_outboundQueue.clear();
    const QJsonArray array = document.array();
    for (const QJsonValue &value : array) {
        if (!value.isObject()) {
            continue;
        }

        const QJsonObject payload = value.toObject();
        const QString clientMessageId = payloadClientMessageId(payload);
        if (queueContainsClientMessageId(m_outboundQueue, clientMessageId)) {
            continue;
        }

        if (m_outboundQueue.size() >= kMaxQueuedMessages) {
            m_outboundQueue.dequeue();
        }
        m_outboundQueue.enqueue(payload);
    }
}

void ChatClient::persistOutboundQueue() const
{
    if (m_outboundQueueStorageKey.trimmed().isEmpty()) {
        return;
    }

    QSettings settings(QStringLiteral("ChatRoom"), QStringLiteral("ChatRoomClient"));
    if (m_outboundQueue.isEmpty()) {
        settings.remove(m_outboundQueueStorageKey);
        return;
    }

    QJsonArray array;
    for (const QJsonObject &payload : m_outboundQueue) {
        array.append(payload);
    }
    settings.setValue(m_outboundQueueStorageKey,
                      QJsonDocument(array).toJson(QJsonDocument::Compact));
}

void ChatClient::scheduleReconnect()
{
#ifdef CHATROOM_HAS_WEBSOCKETS
    if (m_manualDisconnectRequested || !m_serverUrl.isValid()) {
        return;
    }

    auto *socket = qobject_cast<QWebSocket *>(m_socketObject);
    if (!socket || socket->state() == QAbstractSocket::ConnectedState
        || socket->state() == QAbstractSocket::ConnectingState || m_reconnectTimer->isActive()) {
        return;
    }

    int delayMs = kReconnectInitialDelayMs;
    if (m_reconnectAttempt > 0) {
        delayMs = qMin(kReconnectMaxDelayMs, kReconnectInitialDelayMs << qMin(m_reconnectAttempt, 5));
    }
    m_reconnectAttempt += 1;
    m_reconnectTimer->start(delayMs);
    setStatusText(UiText::ChatClient::kReconnectingInPattern.arg(delayMs / 1000));
#endif
}

void ChatClient::stopReconnect()
{
    if (m_reconnectTimer && m_reconnectTimer->isActive()) {
        m_reconnectTimer->stop();
    }
}
