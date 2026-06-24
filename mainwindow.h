#pragma once

#include <functional>
#include <QHash>
#include <QJsonObject>
#include <QJsonArray>
#include <QMainWindow>
#include <QList>
#include <QSet>

#include "networkservice.h"

class ChatClient;
class ChatStore;
class DatabaseManager;
class EmojiPicker;
class ConversationListModel;
class ConversationItemDelegate;
class MessageBubbleDelegate;
class MessageListModel;
class NetworkService;
class ConversationManager;
class MessageHandler;
class ProfileManager;
class QLabel;
class QLineEdit;
class QListView;
class QListWidget;
class QFrame;
class QMessageBox;
class QHttpMultiPart;
class QPushButton;
class QModelIndex;
class QNetworkAccessManager;
class QTimer;
class QUrl;

class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override = default;

    void setAuthSession(const QString &backendBaseUrl, const QString &authToken, const QString &email);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void sendCurrentMessage();
    void sendMediaFile();
    void toggleConnection();
    void createDirectConversation();
    void createGroupConversation();
    void showProfileDialog();
    void showFriendsDialog();
    void showFriendRequestsDialog();
    void logout();
    void showBlacklistDialog();
    void inviteMembersToCurrentConversation();
    void removeMemberFromCurrentConversation();
    void showMembersDialog();
    void leaveCurrentConversation();
    void deleteCurrentConversation();
    void togglePinCurrentConversation();
    void toggleMuteCurrentConversation();
    void onConversationSelected(const QModelIndex &current, const QModelIndex &previous);
    void handleMessageActivated(const QModelIndex &index);
    void editMessage(const QModelIndex &index);
    void refreshConversationHeader();
    void refreshNetworkUi();
    void handleRawMessageReceived(const QString &messageText);
    void handleJsonMessageReceived(const QJsonObject &payload);
    void showQueuedMessagesDialog();
    void runGlobalMessageSearch();
    void scrollMessagesToBottom();

private:
    struct GroupMemberInfo
    {
        QString email;
        QString nickname;
        QString avatarUrl;
        bool isOwner = false;
        bool isSelf = false;
        bool isOnline = false;
        qint64 lastSeenAt = 0;
    };

    struct MessagePaginationState
    {
        qint64 oldestServerMessageId = 0;
        bool hasMore = true;
        bool isLoadingOlder = false;
    };

    void setupUi();
    void setupConnections();
    static QString styleSheet();
    void syncInitialSelection();
    void loadConversationData();
    void loadCurrentUserProfile();
    void loadMessagesForConversation(int index, qint64 beforeId = 0, bool prepend = false);
    void loadConversationMembers(int index);
    void markConversationReadOnServer(const QString &conversationId, qint64 latestServerMessageId);
    QString currentRoomId() const;
    QString currentRoomName() const;
    bool currentConversationIsGroup() const;
    bool selectConversationById(const QString &conversationId);
    void removeMemberByEmail(const QString &memberEmail,
                             std::function<void(bool ok, const QString &errorMessage)> callback);
    void setNetworkStatus(const QString &text, const QString &detail = QString());
    QString networkStatusTextWithQueue(const QString &text) const;
    QString networkStatusDetailWithQueue(const QString &detail = QString()) const;
    void refreshMessageSearchMatches(bool scrollToCurrent = true);
    void jumpMessageSearchMatch(int step);
    void updateMessageSearchUi();
    void focusMessageByServerIdInConversation(const QString &conversationId, qint64 serverMessageId);
    void saveDraftForConversation(const QString &conversationId, const QString &draftText);
    void restoreDraftForConversation(const QString &conversationId);
    void loadDraftsFromSettings();
    void toggleFavoriteForMessageIndex(const QModelIndex &index);
    void recallMessageByIndex(const QModelIndex &index);
    void openMediaLinkByIndex(const QModelIndex &index);
    bool isFavoriteMessage(const QString &conversationId, qint64 serverMessageId) const;
    void showFavoriteMessagesDialog();
    void scheduleTypingStateUpdate();
    void sendTypingState(bool isTyping);
    void updateTypingStatusLabel();
    void clearTypingUsersForConversation(const QString &conversationId);
    void preloadMediaThumbnailsForCurrentConversation();
    void requestMediaThumbnail(qint64 serverMessageId, const QString &rawUrl);
    void preloadMessageAvatarsForCurrentConversation();
    void requestMessageAvatar(const QString &rawUrl, const QString &cacheKey = QString());
    void updateProfileAvatarBadge();

    ChatClient *m_chatClient = nullptr;
    ChatStore *m_chatStore = nullptr;
    DatabaseManager *m_databaseManager = nullptr;
    ConversationListModel *m_conversationModel = nullptr;
    ConversationItemDelegate *m_conversationDelegate = nullptr;
    MessageListModel *m_messageModel = nullptr;
    MessageBubbleDelegate *m_messageDelegate = nullptr;
    NetworkService *m_networkService = nullptr;
    ConversationManager *m_conversationManager = nullptr;
    MessageHandler *m_messageHandler = nullptr;
    ProfileManager *m_profileManager = nullptr;

    QString m_backendBaseUrl;
    QString m_authToken;
    QString m_loggedInUserEmail;
    QString m_loggedInUserNickname;
    QString m_loggedInUserAvatarUrl;
    QString m_currentGroupOwnerName;
    bool m_currentUserOwnsCurrentGroup = false;
    QList<GroupMemberInfo> m_currentConversationMembers;
    QHash<QString, MessagePaginationState> m_messagePaginationStates;
    QHash<QString, qint64> m_pendingMessageFocusByConversationId;
    QTimer *m_typingStopTimer = nullptr;
    QTimer *m_presenceRefreshTimer = nullptr;
    QSet<qint64> m_loadingMediaThumbnailIds;
    QSet<QString> m_loadingAvatarUrls;
    QSet<QString> m_loadedAvatarUrls;
    QHash<QString, QPixmap> m_avatarPixmapsByUrl;
    bool m_suppressTypingSignals = false;
    int m_messageLoadSerial = 0;
    int m_membersLoadSerial = 0;
    QList<int> m_messageSearchMatches;
    int m_messageSearchCurrentIndex = -1;

    QListView *m_conversationListView = nullptr;
    QListView *m_messageListView = nullptr;
    QLabel *m_conversationTitleLabel = nullptr;
    QLabel *m_conversationMetaLabel = nullptr;
    QLabel *m_conversationMembersLabel = nullptr;
    QLabel *m_typingStatusLabel = nullptr;
    QLabel *m_networkStatusLabel = nullptr;
    QPushButton *m_newConversationButton = nullptr;
    QPushButton *m_newGroupButton = nullptr;
    QPushButton *m_profileButton = nullptr;
    QPushButton *m_logoutButton = nullptr;
    QLabel *m_profileAvatarLabel = nullptr;
    QPushButton *m_friendsButton = nullptr;
    QPushButton *m_friendRequestsButton = nullptr;
    QPushButton *m_blacklistButton = nullptr;
    QPushButton *m_showMembersButton = nullptr;
    QPushButton *m_inviteMembersButton = nullptr;
    QPushButton *m_removeMemberButton = nullptr;
    QPushButton *m_leaveConversationButton = nullptr;
    QPushButton *m_showQueueDetailsButton = nullptr;
    QLineEdit *m_serverUrlInput = nullptr;
    QPushButton *m_connectButton = nullptr;
    QLineEdit *m_messageInput = nullptr;
    QLineEdit *m_messageSearchInput = nullptr;
    QLabel *m_messageSearchResultLabel = nullptr;
    QPushButton *m_messageSearchPrevButton = nullptr;
    QPushButton *m_messageSearchNextButton = nullptr;
    QPushButton *m_globalSearchButton = nullptr;
    QPushButton *m_favoriteMessagesButton = nullptr;
    QPushButton *m_sendFileButton = nullptr;
    QPushButton *m_sendButton = nullptr;
    QPushButton *m_emojiButton = nullptr;
    EmojiPicker *m_emojiPicker = nullptr;

    QFrame *m_replyBar = nullptr;
    QLabel *m_replyPreviewLabel = nullptr;
    QPushButton *m_replyCancelButton = nullptr;
    qint64 m_replyToMessageId = 0;
    QString m_replyToContent;
    QString m_replyToSender;

    void startReply(qint64 messageId, const QString &content, const QString &sender);
    void cancelReply();
};
