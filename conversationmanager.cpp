#include "conversationmanager.h"
#include "chatstore.h"
#include "chatutils.h"
#include "conversation.h"
#include "networkservice.h"
#include "uitexts.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QUrl>
#include <QSettings>
#include <QMessageBox>
#include <QInputDialog>

ConversationManager::ConversationManager(ChatStore *chatStore, NetworkService *networkService, QObject *parent)
    : QObject(parent)
    , m_chatStore(chatStore)
    , m_networkService(networkService)
{
}

void ConversationManager::loadConversations(const QString &backendBaseUrl, const QString &loggedInUserEmail)
{
    if (backendBaseUrl.isEmpty()) {
        emit networkStatusChanged(UiText::MainWindow::kStatusMissingSession);
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations"));

    m_networkService->getJsonAsync(url, [this, loggedInUserEmail](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLoadConversationsFailed, result.message);
            return;
        }

        QList<Conversation> conversations;
        const QJsonArray items = result.body.value(QStringLiteral("conversations")).toArray();
        conversations.reserve(items.size());
        for (const QJsonValue &value : items) {
            const QJsonObject object = value.toObject();
            Conversation conversation(object.value(QStringLiteral("id")).toString(),
                                      object.value(QStringLiteral("name")).toString(),
                                      {});
            conversation.type = object.value(QStringLiteral("type")).toString();
            conversation.ownerEmail = object.value(QStringLiteral("ownerEmail")).toString();
            const QJsonObject lastMessageObject = object.value(QStringLiteral("lastMessage")).toObject();
            const QString lastMessageText = lastMessageObject.value(QStringLiteral("text")).toString();
            const QString lastMessageType = lastMessageObject.value(QStringLiteral("messageType")).toString().trimmed().toLower();
            const QString lastSenderEmail = lastMessageObject.value(QStringLiteral("senderEmail")).toString().trimmed();
            if (lastMessageType == QStringLiteral("system")) {
                QString preview = lastMessageText.trimmed();
                if (!preview.isEmpty() && !preview.startsWith(UiText::MessageBubble::kSystemPrefix)) {
                    preview = QStringLiteral("%1 %2").arg(UiText::MessageBubble::kSystemPrefix, preview);
                }
                conversation.lastMessagePreview = preview;
            } else if (!lastMessageText.isEmpty()
                       && !lastSenderEmail.isEmpty()
                       && lastSenderEmail.compare(loggedInUserEmail, Qt::CaseInsensitive) == 0) {
                conversation.lastMessagePreview = UiText::ConversationManager::kYouPrefix.arg(lastMessageText);
            } else {
                conversation.lastMessagePreview = lastMessageText;
            }
            const QString lastCreatedAt = lastMessageObject.value(QStringLiteral("createdAt")).toString().trimmed();
            if (!lastCreatedAt.isEmpty()) {
                conversation.lastMessageTimestamp = ChatUtils::parseBackendTimestamp(lastCreatedAt);
            }
            conversation.memberCount = object.value(QStringLiteral("memberCount")).toInt(0);
            conversation.onlineCount = object.value(QStringLiteral("onlineCount")).toInt(0);
            conversation.unreadCount = object.value(QStringLiteral("unreadCount")).toInt(0);
            conversations.append(std::move(conversation));
        }

        m_chatStore->replaceConversations(std::move(conversations));
        emit conversationsLoaded();
    });
}

void ConversationManager::loadConversationMembers(const QString &backendBaseUrl, int index)
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
    url.setPath(QStringLiteral("/api/conversations/%1/members").arg(conversationId));

    m_networkService->getJsonAsync(url, [this, index, conversationId](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kMembersLoadFailed, result.message);
            return;
        }

        emit conversationMembersLoaded(conversationId);
    });
}

void ConversationManager::createDirectConversation(const QString &backendBaseUrl, const QString &peerEmail)
{
    if (backendBaseUrl.isEmpty()) {
        emit networkStatusChanged(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/direct"));

    m_networkService->postJsonAsync(url, QJsonObject{{QStringLiteral("peerEmail"), peerEmail}},
                  [this, peerEmail](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusCreateDirectFailed, result.message);
                          emit conversationError(result.message);
                          return;
                      }

                      const QString conversationId = result.body.value(QStringLiteral("conversation"))
                                                         .toObject()
                                                         .value(QStringLiteral("id"))
                                                         .toString();

                      loadConversations(m_networkService->backendBaseUrl(), QString());
                      if (!conversationId.isEmpty()) {
                          selectConversationById(conversationId);
                      }

                      emit networkStatusChanged(UiText::MainWindow::kStatusDirectCreated, peerEmail);
                      emit conversationCreated(conversationId);
                  });
}

void ConversationManager::createGroupConversation(const QString &backendBaseUrl, const QString &groupName, const QJsonArray &memberEmails)
{
    if (backendBaseUrl.isEmpty()) {
        emit networkStatusChanged(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/group"));

    m_networkService->postJsonAsync(url,
                  QJsonObject{
                      {QStringLiteral("name"), groupName},
                      {QStringLiteral("memberEmails"), memberEmails},
                  },
                  [this, groupName](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusCreateGroupFailed, result.message);
                          emit conversationError(result.message);
                          return;
                      }

                      const QString conversationId = result.body.value(QStringLiteral("conversation"))
                                                         .toObject()
                                                         .value(QStringLiteral("id"))
                                                         .toString();

                      loadConversations(m_networkService->backendBaseUrl(), QString());
                      if (!conversationId.isEmpty()) {
                          selectConversationById(conversationId);
                      }

                      emit networkStatusChanged(UiText::MainWindow::kStatusGroupCreated, groupName);
                      emit conversationCreated(conversationId);
                  });
}

void ConversationManager::inviteMembers(const QString &backendBaseUrl, const QString &conversationId, const QJsonArray &memberEmails)
{
    if (backendBaseUrl.isEmpty() || conversationId.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/members").arg(conversationId));

    m_networkService->postJsonAsync(url, QJsonObject{{QStringLiteral("memberEmails"), memberEmails}},
                  [this](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusInviteFailed, result.message);
                          emit conversationError(result.message);
                          return;
                      }

                      emit networkStatusChanged(
                          UiText::MainWindow::kStatusMembersInvited,
                          UiText::MainWindow::kAddedMembersPattern
                              .arg(result.body.value(QStringLiteral("addedCount")).toInt()));
                  });
}

void ConversationManager::removeMember(const QString &backendBaseUrl, const QString &conversationId, const QString &memberEmail)
{
    if (backendBaseUrl.isEmpty() || conversationId.isEmpty() || memberEmail.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/remove-member").arg(conversationId));

    m_networkService->postJsonAsync(url, QJsonObject{{QStringLiteral("memberEmail"), memberEmail}},
                  [this, memberEmail](const NetworkService::HttpResult &result) {
                      if (!result.ok) {
                          emit networkStatusChanged(UiText::MainWindow::kStatusRemoveMemberFailed, result.message);
                          emit conversationError(result.message);
                          return;
                      }

                      emit networkStatusChanged(UiText::MainWindow::kStatusMemberRemoved, memberEmail);
                  });
}

void ConversationManager::leaveConversation(const QString &backendBaseUrl, const QString &conversationId)
{
    if (backendBaseUrl.isEmpty() || conversationId.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/leave").arg(conversationId));

    m_networkService->postJsonAsync(url, QJsonObject{}, [this](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            emit networkStatusChanged(UiText::MainWindow::kStatusLeaveGroupFailed, result.message);
            emit conversationError(result.message);
            return;
        }

        loadConversations(m_networkService->backendBaseUrl(), QString());
        emit networkStatusChanged(UiText::MainWindow::kStatusLeftGroup);
    });
}

void ConversationManager::markConversationRead(const QString &backendBaseUrl, const QString &conversationId, qint64 messageId)
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

QString ConversationManager::currentRoomId() const
{
    const Conversation *conversation = m_chatStore->currentConversation();
    return conversation ? conversation->id : QStringLiteral("lobby");
}

QString ConversationManager::currentRoomName() const
{
    const Conversation *conversation = m_chatStore->currentConversation();
    return conversation ? conversation->name : UiText::MainWindow::kLobby;
}

bool ConversationManager::currentConversationIsGroup() const
{
    const Conversation *conversation = m_chatStore->currentConversation();
    return conversation && conversation->type == QStringLiteral("group");
}

bool ConversationManager::selectConversationById(const QString &conversationId)
{
    if (conversationId.trimmed().isEmpty()) {
        return false;
    }

    const QList<Conversation> &conversations = m_chatStore->conversations();
    for (int i = 0; i < conversations.size(); ++i) {
        if (conversations.at(i).id == conversationId) {
            m_chatStore->setCurrentConversation(i);
            return true;
        }
    }

    return false;
}

void ConversationManager::deleteCurrentConversation()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    m_pinnedConversationIds.remove(conversation->id);
    m_mutedConversationIds.remove(conversation->id);
    loadConversations(m_networkService->backendBaseUrl(), QString());
    emit networkStatusChanged(UiText::ConversationManager::kConversationDeleted, conversation->name);
}

void ConversationManager::togglePinCurrentConversation()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    if (m_pinnedConversationIds.contains(conversation->id)) {
        m_pinnedConversationIds.remove(conversation->id);
        emit networkStatusChanged(UiText::ConversationManager::kUnpinned, conversation->name);
    } else {
        m_pinnedConversationIds.insert(conversation->id);
        emit networkStatusChanged(UiText::ConversationManager::kPinned, conversation->name);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("pinned_conversations"), QStringList(m_pinnedConversationIds.values()));
    settings.sync();
}

void ConversationManager::toggleMuteCurrentConversation()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    if (m_mutedConversationIds.contains(conversation->id)) {
        m_mutedConversationIds.remove(conversation->id);
        emit networkStatusChanged(UiText::ConversationManager::kUnmuted, conversation->name);
    } else {
        m_mutedConversationIds.insert(conversation->id);
        emit networkStatusChanged(UiText::ConversationManager::kMuted, conversation->name);
    }

    QSettings settings;
    settings.setValue(QStringLiteral("muted_conversations"), QStringList(m_mutedConversationIds.values()));
    settings.sync();
}

bool ConversationManager::isConversationPinned(const QString &conversationId) const
{
    return m_pinnedConversationIds.contains(conversationId);
}

bool ConversationManager::isConversationMuted(const QString &conversationId) const
{
    return m_mutedConversationIds.contains(conversationId);
}

int ConversationManager::currentConversationIndex() const
{
    return m_chatStore->currentConversationIndex();
}

const Conversation* ConversationManager::currentConversation() const
{
    return m_chatStore->currentConversation();
}

