#pragma once

#include <QJsonObject>
#include <QObject>
#include <QQueue>
#include <QUrl>
#include <QtTypes>

class QTimer;

class ChatClient : public QObject
{
    Q_OBJECT

public:
    explicit ChatClient(QObject *parent = nullptr);

    bool isAvailable() const;
    bool isConnected() const;
    QString statusText() const;

    void connectToServer(const QUrl &url, const QString &authToken = QString());
    void disconnectFromServer();
    void joinConversation(const QString &conversationId);
    void sendChatMessage(const QString &text, const QString &conversationId,
                         const QString &clientMessageId = QString());
    void recallMessage(const QString &conversationId, qint64 messageId);
    void setTypingState(const QString &conversationId, bool isTyping);

signals:
    void availabilityChanged(bool available);
    void connectionStateChanged(const QString &statusText);
    void rawMessageReceived(const QString &messageText);
    void jsonMessageReceived(const QJsonObject &payload);
    void errorOccurred(const QString &errorText);
    void connected();
    void disconnected();

private:
    void openSocket();
    void sendJson(const QJsonObject &payload);
    void flushQueuedMessages();
    void restoreOutboundQueue();
    void persistOutboundQueue() const;
    QString makeQueueStorageKey(const QUrl &serverUrl, const QString &authToken) const;
    void scheduleReconnect();
    void stopReconnect();
    void setStatusText(const QString &statusText);

    QString m_statusText;
    bool m_available = false;
    bool m_connected = false;
    bool m_manualDisconnectRequested = false;
    QObject *m_socketObject = nullptr;
    QTimer *m_reconnectTimer = nullptr;
    QString m_authToken;
    QUrl m_serverUrl;
    int m_reconnectAttempt = 0;
    QQueue<QJsonObject> m_outboundQueue;
    QString m_outboundQueueStorageKey;
};
