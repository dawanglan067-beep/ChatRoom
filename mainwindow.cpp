#include "mainwindow.h"

#include "chatclient.h"
#include "chatstore.h"
#include "chatutils.h"
#include "conversation.h"
#include "conversationitemdelegate.h"
#include "conversationlistmodel.h"
#include "conversationmanager.h"
#include "databasemanager.h"
#include "emojipicker.h"
#include "mediautils.h"
#include "messagebubbledelegate.h"
#include "messagehandler.h"
#include "messagelistmodel.h"
#include "profilemanager.h"
#include "timeformatutils.h"
#include "uitexts.h"

#include <algorithm>
#include <QAbstractItemView>
#include <QDateTime>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFrame>
#include <QFormLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QItemSelectionModel>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPixmap>
#include <QPushButton>
#include <QRegularExpression>
#include <QSettings>
#include <QSignalBlocker>
#include <QStandardPaths>
#include <QClipboard>
#include <QDesktopServices>
#include <QScrollBar>
#include <QScrollArea>
#include <QStringList>
#include <QTimer>
#include <QUuid>
#include <QUrl>
#include <QUrlQuery>
#include <QVBoxLayout>
#include <QWidget>

namespace
{
constexpr qint64 kMessageRecallWindowMs = 2 * 60 * 1000;
constexpr int kTypingStopDelayMs = 1800;

qint64 oldestLoadedServerMessageId(const Conversation *conversation)
{
    if (!conversation) {
        return 0;
    }

    for (const Message &message : conversation->messages) {
        if (message.serverMessageId > 0) {
            return message.serverMessageId;
        }
    }

    return 0;
}

constexpr int kMessagePageSize = 50;
constexpr int kOlderMessagesLoadTrigger = 24;

bool canRecallMessageLocally(const QModelIndex &index)
{
    if (!index.isValid()) {
        return false;
    }

    if (!index.data(MessageListModel::IsSelfRole).toBool()) {
        return false;
    }

    const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
    const qint64 timestamp = index.data(MessageListModel::TimestampRole).toLongLong();
    const QString senderId = index.data(MessageListModel::SenderIdRole).toString().trimmed();
    const QString content = index.data(MessageListModel::ContentRole).toString().trimmed();
    const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
    if (serverMessageId <= 0 || timestamp <= 0 || nowMs < timestamp) {
        return false;
    }
    if (senderId.compare(UiText::MessageBubble::kSystemSenderKey, Qt::CaseInsensitive) == 0
        || content.startsWith(UiText::MessageBubble::kSystemPrefix)) {
        return false;
    }

    return (nowMs - timestamp) <= kMessageRecallWindowMs;
}

QString favoriteMessageKey(const QString &conversationId, qint64 serverMessageId)
{
    return QStringLiteral("%1#%2").arg(conversationId).arg(serverMessageId);
}

void showImagePreviewDialog(QWidget *parent, const QString &fileName, const QPixmap &pixmap, const QString &localPath)
{
    if (pixmap.isNull()) {
        return;
    }

    QDialog dialog(parent);
    dialog.setWindowTitle(fileName.trimmed().isEmpty() ? UiText::MainWindow::kImagePreviewTitle : fileName.trimmed());
    dialog.resize(820, 620);

    auto *layout = new QVBoxLayout(&dialog);
    auto *scrollArea = new QScrollArea(&dialog);
    scrollArea->setWidgetResizable(true);
    scrollArea->setFrameShape(QFrame::NoFrame);

    auto *imageLabel = new QLabel(scrollArea);
    imageLabel->setAlignment(Qt::AlignCenter);
    imageLabel->setMinimumSize(360, 260);
    imageLabel->setStyleSheet(QStringLiteral("background: #111827; border-radius: 12px;"));
    const QSize targetSize(760, 500);
    imageLabel->setPixmap(pixmap.scaled(targetSize, Qt::KeepAspectRatio, Qt::SmoothTransformation));
    scrollArea->setWidget(imageLabel);
    layout->addWidget(scrollArea, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    QPushButton *openButton = buttonBox->addButton(UiText::MainWindow::kOpenWithSystem, QDialogButtonBox::ActionRole);
    QPushButton *closeButton = buttonBox->addButton(UiText::MainWindow::kClose, QDialogButtonBox::RejectRole);
    QObject::connect(openButton, &QPushButton::clicked, &dialog, [localPath]() {
        QDesktopServices::openUrl(QUrl::fromLocalFile(localPath));
    });
    QObject::connect(closeButton, &QPushButton::clicked, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog.exec();
}
}

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
    , m_chatClient(new ChatClient(this))
    , m_chatStore(new ChatStore(this))
    , m_conversationModel(new ConversationListModel(m_chatStore, this))
    , m_conversationDelegate(new ConversationItemDelegate(this))
    , m_messageModel(new MessageListModel(m_chatStore, this))
    , m_messageDelegate(new MessageBubbleDelegate(this))
    , m_networkService(new NetworkService(this))
    , m_conversationManager(new ConversationManager(m_chatStore, m_networkService, this))
    , m_messageHandler(new MessageHandler(m_chatStore, m_chatClient, m_networkService, this))
    , m_profileManager(new ProfileManager(m_networkService, m_chatClient, this))
    , m_databaseManager(new DatabaseManager(this))
{
    setupUi();
    setupConnections();
    m_typingStopTimer = new QTimer(this);
    m_typingStopTimer->setSingleShot(true);
    connect(m_typingStopTimer, &QTimer::timeout, this, [this]() {
        sendTypingState(false);
    });
    m_presenceRefreshTimer = new QTimer(this);
    m_presenceRefreshTimer->setSingleShot(true);
    connect(m_presenceRefreshTimer, &QTimer::timeout, this, [this]() {
        refreshConversationHeader();
    });

    updateProfileAvatarBadge();
    refreshConversationHeader();
    refreshNetworkUi();
}

void MainWindow::setAuthSession(const QString &backendBaseUrl, const QString &authToken, const QString &email)
{
    m_backendBaseUrl = backendBaseUrl.trimmed();
    m_authToken = authToken.trimmed();
    m_loggedInUserEmail = email.trimmed();
    QSettings settings;
    m_loggedInUserNickname = settings.value(QStringLiteral("auth/user_nickname")).toString().trimmed();
    m_loggedInUserAvatarUrl = settings.value(QStringLiteral("auth/user_avatar_url")).toString().trimmed();
    loadDraftsFromSettings();
    loadFavoritesFromSettings();

    if (!m_backendBaseUrl.isEmpty()) {
        QUrl wsUrl = QUrl::fromUserInput(m_backendBaseUrl);
        wsUrl.setScheme(wsUrl.scheme() == QStringLiteral("https") ? QStringLiteral("wss")
                                                                  : QStringLiteral("ws"));
        wsUrl.setPath(QStringLiteral("/ws"));
        wsUrl.setQuery(QString());
        m_serverUrlInput->setText(wsUrl.toString());
    }

    m_networkService->setAuthToken(m_authToken);
    m_networkService->setBackendBaseUrl(m_backendBaseUrl);
    m_messageHandler->setLoggedInEmail(m_loggedInUserEmail);

    if (m_databaseManager->open(m_loggedInUserEmail)) {
        const QList<Conversation> cached = m_databaseManager->loadConversations();
        if (!cached.isEmpty()) {
            m_chatStore->replaceConversations(cached);
            syncInitialSelection();
        }
    }

    loadCurrentUserProfile();
    loadConversationData();
    updateProfileAvatarBadge();
    refreshConversationHeader();
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_profileAvatarLabel && event->type() == QEvent::MouseButtonRelease) {
        showProfileDialog();
        return true;
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::sendCurrentMessage()
{
    if (!m_messageInput || !m_chatStore || !m_chatClient) {
        return;
    }

    const QString messageText = m_messageInput->text().trimmed();
    if (messageText.isEmpty()) {
        return;
    }

    const QString conversationId = currentRoomId();
    if (conversationId.trimmed().isEmpty()) {
        return;
    }

    const QString clientMessageId = QUuid::createUuid().toString(QUuid::WithoutBraces);

    QString displayContent = messageText;
    if (m_replyToMessageId > 0) {
        displayContent = QStringLiteral("↩ 回复 %1: %2\n%3")
            .arg(m_replyToSender, m_replyToContent.left(60), messageText);
    }

    QSignalBlocker blocker(m_messageInput);
    m_messageInput->clear();
    blocker.unblock();
    sendTypingState(false);

    if (!m_chatStore->addPendingMessageToCurrentChat(displayContent, clientMessageId)) {
        return;
    }
    m_pendingMessageConversationIds.insert(clientMessageId, conversationId);

    if (!m_chatClient->isConnected()) {
        m_chatStore->markMessageQueued(conversationId, clientMessageId);
        m_messageHandler->restartPendingMessageTimeout(conversationId, clientMessageId);
        setNetworkStatus(UiText::MainWindow::kStatusConnectRealtimeFirst,
                         UiText::MainWindow::kStatusMessageQueuedDetail);
        m_chatClient->connectToServer(QUrl::fromUserInput(m_serverUrlInput->text().trimmed()), m_authToken);
        m_chatClient->sendChatMessage(messageText, conversationId, clientMessageId);
        saveDraftForConversation(conversationId, QString());
        cancelReply();
        return;
    }

    m_messageHandler->restartPendingMessageTimeout(conversationId, clientMessageId);
    m_chatClient->sendChatMessage(messageText, conversationId, clientMessageId);
    saveDraftForConversation(conversationId, QString());
    cancelReply();
}

void MainWindow::sendMediaFile()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || currentRoomId().trimmed().isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSelectConversationFirst);
        return;
    }

    const QString selectedPath = QFileDialog::getOpenFileName(
        this,
        UiText::MainWindow::kSelectFileTitle,
        QString(),
        UiText::MainWindow::kAllFilesFilter);
    if (selectedPath.trimmed().isEmpty()) {
        return;
    }

    m_messageHandler->sendMediaFile(m_backendBaseUrl, selectedPath, conversation->id, this);
}

void MainWindow::toggleConnection()
{
    if (!m_chatClient->isAvailable()) {
        refreshNetworkUi();
        return;
    }

    if (m_chatClient->isConnected()) {
        sendTypingState(false);
        m_chatClient->disconnectFromServer();
        return;
    }

    const QUrl serverUrl = QUrl::fromUserInput(m_serverUrlInput->text().trimmed());
    if (!serverUrl.isValid()) {
        setNetworkStatus(UiText::MainWindow::kStatusInvalidAddress,
                         UiText::MainWindow::kInvalidWebSocketUrl);
        return;
    }

    m_chatClient->connectToServer(serverUrl, m_authToken);
}

void MainWindow::createDirectConversation()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    bool accepted = false;
    const QString peerEmail = QInputDialog::getText(
                                  this,
                                  UiText::MainWindow::kDialogNewDirect,
                                  UiText::MainWindow::kDialogNewDirectPrompt,
                                  QLineEdit::Normal,
                                  QString(),
                                  &accepted)
                                  .trimmed()
                                  .toLower();

    if (!accepted || peerEmail.isEmpty()) {
        return;
    }

    m_conversationManager->createDirectConversation(m_backendBaseUrl, peerEmail);
}

void MainWindow::createGroupConversation()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("创建群聊"));
    dialog.resize(460, 420);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto *nameLabel = new QLabel(QStringLiteral("群名称"), &dialog);
    nameLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    auto *nameInput = new QLineEdit(&dialog);
    nameInput->setPlaceholderText(QStringLiteral("输入群聊名称"));
    nameInput->setMinimumHeight(40);
    layout->addWidget(nameLabel);
    layout->addWidget(nameInput);

    auto *memberLabel = new QLabel(QStringLiteral("邀请成员"), &dialog);
    memberLabel->setStyleSheet(QStringLiteral("font-weight: 600;"));
    layout->addWidget(memberLabel);

    auto *addLayout = new QHBoxLayout();
    auto *emailInput = new QLineEdit(&dialog);
    emailInput->setPlaceholderText(QStringLiteral("输入邮箱，回车添加"));
    emailInput->setMinimumHeight(40);
    auto *addButton = new QPushButton(QStringLiteral("添加"), &dialog);
    addButton->setMinimumHeight(40);
    addLayout->addWidget(emailInput, 1);
    addLayout->addWidget(addButton);
    layout->addLayout(addLayout);

    auto *memberList = new QListWidget(&dialog);
    memberList->setMinimumHeight(120);
    layout->addWidget(memberList, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    auto *createButton = buttonBox->addButton(QStringLiteral("创建群聊"), QDialogButtonBox::AcceptRole);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    createButton->setEnabled(false);
    layout->addWidget(buttonBox);

    auto updateCreateButton = [nameInput, memberList, createButton]() {
        createButton->setEnabled(!nameInput->text().trimmed().isEmpty() && memberList->count() > 0);
    };

    connect(nameInput, &QLineEdit::textChanged, &dialog, updateCreateButton);

    auto addMember = [emailInput, memberList, updateCreateButton]() {
        const QString email = emailInput->text().trimmed().toLower();
        if (email.isEmpty()) return;

        for (int i = 0; i < memberList->count(); ++i) {
            if (memberList->item(i)->data(Qt::UserRole).toString() == email) {
                emailInput->clear();
                return;
            }
        }

        auto *item = new QListWidgetItem(email, memberList);
        item->setData(Qt::UserRole, email);
        emailInput->clear();
        updateCreateButton();
    };

    connect(addButton, &QPushButton::clicked, &dialog, addMember);
    connect(emailInput, &QLineEdit::returnPressed, &dialog, addMember);

    connect(memberList, &QListWidget::itemDoubleClicked, &dialog, [memberList, updateCreateButton](QListWidgetItem *item) {
        delete memberList->takeItem(memberList->row(item));
        updateCreateButton();
    });

    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [&dialog]() {
        dialog.accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    if (dialog.exec() != QDialog::Accepted) {
        return;
    }

    const QString groupName = nameInput->text().trimmed();
    QJsonArray memberEmails;
    for (int i = 0; i < memberList->count(); ++i) {
        memberEmails.append(memberList->item(i)->data(Qt::UserRole).toString());
    }

    if (groupName.isEmpty() || memberEmails.isEmpty()) {
        return;
    }

    m_conversationManager->createGroupConversation(m_backendBaseUrl, groupName, memberEmails);
}

void MainWindow::focusMessageByServerIdInConversation(const QString &conversationId, qint64 serverMessageId)
{
    if (!m_messageListView || !m_messageModel || conversationId.trimmed().isEmpty() || serverMessageId <= 0) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversation->id != conversationId) {
        return;
    }

    for (int row = 0; row < conversation->messages.size(); ++row) {
        if (conversation->messages.at(row).serverMessageId != serverMessageId) {
            continue;
        }

        const QModelIndex targetIndex = m_messageModel->index(row, 0);
        if (targetIndex.isValid()) {
            m_messageListView->scrollTo(targetIndex, QListView::PositionAtCenter);
        }
        m_pendingMessageFocusByConversationId.remove(conversationId);
        return;
    }
}


void MainWindow::saveDraftForConversation(const QString &conversationId, const QString &draftText)
{
    if (!m_chatStore || conversationId.trimmed().isEmpty()) {
        return;
    }
    if (m_chatStore->findConversationIndex(conversationId) < 0) {
        return;
    }

    const QString normalized = draftText;
    if (normalized.trimmed().isEmpty()) {
        m_messageHandler->saveDraft(conversationId, QString());
        m_chatStore->setConversationDraft(conversationId, QString());
    } else {
        m_messageHandler->saveDraft(conversationId, normalized);
        m_chatStore->setConversationDraft(conversationId, normalized);
    }

    m_messageHandler->persistDraftsToSettings();
}

void MainWindow::restoreDraftForConversation(const QString &conversationId)
{
    if (!m_messageInput) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    const QString draft = conversation ? conversation->draftText : QString();
    m_suppressTypingSignals = true;
    const QSignalBlocker blocker(m_messageInput);
    m_messageInput->setText(draft);
    m_suppressTypingSignals = false;
}

void MainWindow::loadDraftsFromSettings()
{
    m_messageHandler->loadDraftsFromSettings();

    if (m_loggedInUserEmail.trimmed().isEmpty()) {
        return;
    }

    QSettings settings(QStringLiteral("ChatRoom"), QStringLiteral("ChatRoomClient"));
    const QString key = QStringLiteral("drafts/%1").arg(m_loggedInUserEmail.trimmed().toLower());
    const QByteArray raw = settings.value(key).toByteArray();
    if (raw.isEmpty()) {
        return;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(raw, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return;
    }

    const QJsonObject object = document.object();
    for (auto it = object.constBegin(); it != object.constEnd(); ++it) {
        const QString conversationId = it.key().trimmed();
        const QString draft = it.value().toString();
        if (conversationId.isEmpty() || draft.trimmed().isEmpty()) {
            continue;
        }
        m_messageHandler->saveDraft(conversationId, draft);
        m_chatStore->setConversationDraft(conversationId, draft);
    }
}

void MainWindow::persistDraftsToSettings() const
{
    m_messageHandler->persistDraftsToSettings();
}

bool MainWindow::isFavoriteMessage(const QString &conversationId, qint64 serverMessageId) const
{
    if (conversationId.trimmed().isEmpty() || serverMessageId <= 0) {
        return false;
    }
    return m_favoriteMessagesByKey.contains(favoriteMessageKey(conversationId, serverMessageId));
}

void MainWindow::toggleFavoriteForMessageIndex(const QModelIndex &index)
{
    if (!index.isValid() || !m_chatStore) {
        return;
    }

    const QString conversationId = currentRoomId();
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversationId.trimmed().isEmpty()) {
        return;
    }

    const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
    if (serverMessageId <= 0) {
        return;
    }

    const QString key = favoriteMessageKey(conversationId, serverMessageId);
    if (m_favoriteMessagesByKey.contains(key)) {
        m_favoriteMessagesByKey.remove(key);
        persistFavoritesToSettings();
        m_messageHandler->refreshFavoriteHighlights();
        setNetworkStatus(UiText::MainWindow::kStatusFavoriteRemoved,
                         QStringLiteral("消息 ID: %1").arg(serverMessageId));
        return;
    }

    QJsonObject object;
    object.insert(QStringLiteral("conversationId"), conversationId);
    object.insert(QStringLiteral("conversationName"), conversation->name);
    object.insert(QStringLiteral("serverMessageId"), serverMessageId);
    object.insert(QStringLiteral("content"), index.data(MessageListModel::ContentRole).toString());
    object.insert(QStringLiteral("senderId"), index.data(MessageListModel::SenderIdRole).toString());
    object.insert(QStringLiteral("timestamp"), QString::number(index.data(MessageListModel::TimestampRole).toLongLong()));
    object.insert(QStringLiteral("favoritedAt"), QString::number(QDateTime::currentMSecsSinceEpoch()));
    m_favoriteMessagesByKey.insert(key, object);
    persistFavoritesToSettings();
    m_messageHandler->refreshFavoriteHighlights();
    setNetworkStatus(UiText::MainWindow::kStatusFavorited,
                     QStringLiteral("消息 ID: %1").arg(serverMessageId));
}

void MainWindow::recallMessageByIndex(const QModelIndex &index)
{
    if (!index.isValid() || !m_chatClient || !m_chatStore) {
        return;
    }

    const QString conversationId = currentRoomId().trimmed();
    if (conversationId.isEmpty()) {
        return;
    }

    if (!canRecallMessageLocally(index)) {
        setNetworkStatus(UiText::MainWindow::kStatusRecallFailed,
                         UiText::MainWindow::kDetailRecallWindowLimited);
        return;
    }

    const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
    if (serverMessageId <= 0) {
        return;
    }

    const QString content = index.data(MessageListModel::ContentRole).toString();
    const int maxPreviewLen = 30;
    const QString preview = content.length() > maxPreviewLen
        ? content.left(maxPreviewLen) + QStringLiteral("...")
        : content;

    const auto decision = QMessageBox::question(
        this,
        QStringLiteral("撤回消息"),
        QStringLiteral("确定要撤回这条消息吗？\n\n\"%1\"").arg(preview));

    if (decision != QMessageBox::Yes) {
        return;
    }

    m_chatClient->recallMessage(conversationId, serverMessageId);
    setNetworkStatus(UiText::MainWindow::kStatusRecallRequested,
                     QStringLiteral("消息 ID: %1").arg(serverMessageId));
}

void MainWindow::openMediaLinkByIndex(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    QString fileName;
    QString rawUrl;
    bool isImage = false;
    const QString content = index.data(MessageListModel::ContentRole).toString();
    if (!MediaUtils::parseMediaMessageContent(content, &isImage, &fileName, &rawUrl)) {
        return;
    }

    const QUrl url = MediaUtils::resolveMediaUrl(rawUrl, m_backendBaseUrl);
    if (!url.isValid()) {
        setNetworkStatus(UiText::MainWindow::kStatusOpenFileFailed, UiText::MainWindow::kDetailInvalidLink);
        return;
    }

    if (!MediaUtils::isBackendUploadPath(url)) {
        if (!QDesktopServices::openUrl(url)) {
            setNetworkStatus(UiText::MainWindow::kStatusOpenFileFailed, fileName.isEmpty() ? url.toString() : fileName);
            return;
        }
        setNetworkStatus(UiText::MainWindow::kStatusFileOpened, fileName.isEmpty() ? url.toString() : fileName);
        return;
    }

    if (!m_networkService->networkManager() || m_authToken.trimmed().isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusOpenFileFailed, UiText::MainWindow::kDetailMissingAuthSession);
        return;
    }

    QNetworkRequest request(url);
    request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_authToken).toUtf8());
    QNetworkReply *reply = m_networkService->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, fileName, url, isImage]() {
        const int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const QByteArray bytes = reply->readAll();
        const bool requestOk = (reply->error() == QNetworkReply::NoError)
                               && statusCode >= 200 && statusCode < 300
                               && !bytes.isEmpty();
        reply->deleteLater();

        if (!requestOk) {
            setNetworkStatus(QStringLiteral("状态：打开文件失败"),
                             UiText::MainWindow::kDetailDownloadFailedPattern.arg(statusCode));
            return;
        }

        const QString baseName = fileName.trimmed().isEmpty()
            ? QFileInfo(url.path()).fileName()
            : fileName.trimmed();
        const QString tempDirPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
        QDir().mkpath(tempDirPath);
        const QString localPath = QDir(tempDirPath).filePath(
            QStringLiteral("chatroom_media_%1_%2")
                .arg(QString::number(QDateTime::currentMSecsSinceEpoch()), baseName));

        QFile localFile(localPath);
        if (!localFile.open(QIODevice::WriteOnly) || localFile.write(bytes) != bytes.size()) {
            setNetworkStatus(UiText::MainWindow::kStatusOpenFileFailed, UiText::MainWindow::kDetailSaveTempFileFailed);
            return;
        }
        localFile.close();

        if (isImage) {
            QPixmap pixmap;
            if (pixmap.loadFromData(bytes)) {
                showImagePreviewDialog(this, baseName, pixmap, localPath);
                setNetworkStatus(QStringLiteral("状态：图片已打开"), baseName);
                return;
            }
        }

        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(localPath))) {
            setNetworkStatus(UiText::MainWindow::kStatusOpenFileFailed, UiText::MainWindow::kDetailOpenDownloadedFileFailed);
            return;
        }

        setNetworkStatus(UiText::MainWindow::kStatusFileOpened, baseName);
    });
}

void MainWindow::preloadMediaThumbnailsForCurrentConversation()
{
    if (!m_chatStore || !m_messageDelegate) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    for (const Message &message : conversation->messages) {
        if (message.serverMessageId <= 0) {
            continue;
        }
        bool isImage = false;
        QString fileName;
        QString rawUrl;
        if (!MediaUtils::parseMediaMessageContent(message.content, &isImage, &fileName, &rawUrl) || !isImage) {
            continue;
        }
        requestMediaThumbnail(message.serverMessageId, rawUrl);
    }
}

void MainWindow::requestMediaThumbnail(qint64 serverMessageId, const QString &rawUrl)
{
    if (serverMessageId <= 0 || rawUrl.trimmed().isEmpty() || !m_networkService->networkManager() || !m_messageDelegate) {
        return;
    }
    if (m_loadingMediaThumbnailIds.contains(serverMessageId)) {
        return;
    }
    if (m_messageDelegate && m_messageDelegate->property("skipLoaded").toBool()) {
        return;
    }

    const QUrl url = MediaUtils::resolveMediaUrl(rawUrl, m_backendBaseUrl);
    if (!url.isValid()) {
        return;
    }

    m_loadingMediaThumbnailIds.insert(serverMessageId);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    if (!m_authToken.trimmed().isEmpty() && MediaUtils::isBackendUploadPath(url)) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_authToken).toUtf8());
    }
    QNetworkReply *reply = m_networkService->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, serverMessageId]() {
        m_loadingMediaThumbnailIds.remove(serverMessageId);
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        if (bytes.isEmpty()) {
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(bytes)) {
            return;
        }

        const int maxSize = 400;
        if (pixmap.width() > maxSize || pixmap.height() > maxSize) {
            pixmap = pixmap.scaled(maxSize, maxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        m_messageDelegate->setMediaThumbnail(serverMessageId, pixmap);
        if (m_messageListView && m_messageListView->viewport()) {
            m_messageListView->viewport()->update();
        }
    });
}

void MainWindow::preloadMessageAvatarsForCurrentConversation()
{
    if (!m_chatStore || !m_messageDelegate) {
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    QHash<QString, QString> memberAvatarByEmail;
    QHash<QString, QString> memberAvatarByNickname;
    for (const GroupMemberInfo &member : std::as_const(m_currentConversationMembers)) {
        if (member.isSelf || member.avatarUrl.trimmed().isEmpty()) {
            continue;
        }
        if (!member.email.trimmed().isEmpty()) {
            memberAvatarByEmail.insert(member.email.toLower(), member.avatarUrl.trimmed());
        }
        if (!member.nickname.trimmed().isEmpty()) {
            memberAvatarByNickname.insert(member.nickname.toLower(), member.avatarUrl.trimmed());
        }
    }

    for (const Message &message : conversation->messages) {
        if (message.isSelf) {
            continue;
        }

        QString avatarUrl = message.senderAvatarUrl.trimmed();
        if (avatarUrl.isEmpty()) {
            const QString senderKey = message.senderId.trimmed().toLower();
            avatarUrl = memberAvatarByEmail.value(senderKey);
            if (avatarUrl.isEmpty()) {
                avatarUrl = memberAvatarByNickname.value(senderKey);
            }
        }

        if (!avatarUrl.isEmpty()) {
            requestMessageAvatar(avatarUrl, message.senderId);
        }
    }
}

void MainWindow::requestMessageAvatar(const QString &rawUrl, const QString &cacheKey)
{
    const QString normalizedUrl = rawUrl.trimmed();
    if (normalizedUrl.isEmpty() || !m_networkService->networkManager() || !m_messageDelegate) {
        return;
    }
    const QString normalizedCacheKey = cacheKey.trimmed();
    if (m_loadedAvatarUrls.contains(normalizedUrl)) {
        const QPixmap cachedPixmap = m_avatarPixmapsByUrl.value(normalizedUrl);
        if (!cachedPixmap.isNull() && !normalizedCacheKey.isEmpty()) {
            m_messageDelegate->setSenderAvatarPixmap(normalizedCacheKey, cachedPixmap);
            if (m_messageListView && m_messageListView->viewport()) {
                m_messageListView->viewport()->update();
            }
        }
        return;
    }
    if (m_loadingAvatarUrls.contains(normalizedUrl)) {
        return;
    }

    const QUrl url = MediaUtils::resolveMediaUrl(normalizedUrl, m_backendBaseUrl);
    if (!url.isValid()) {
        return;
    }

    m_loadingAvatarUrls.insert(normalizedUrl);
    QNetworkRequest request(url);
    if (!m_authToken.trimmed().isEmpty() && MediaUtils::isBackendUploadPath(url)) {
        request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_authToken).toUtf8());
    }

    QNetworkReply *reply = m_networkService->networkManager()->get(request);
    connect(reply, &QNetworkReply::finished, this, [this, reply, normalizedUrl, normalizedCacheKey]() {
        m_loadingAvatarUrls.remove(normalizedUrl);
        const QByteArray bytes = reply->readAll();
        reply->deleteLater();
        if (bytes.isEmpty()) {
            return;
        }

        QPixmap pixmap;
        if (!pixmap.loadFromData(bytes)) {
            return;
        }

        m_loadedAvatarUrls.insert(normalizedUrl);
        m_avatarPixmapsByUrl.insert(normalizedUrl, pixmap);
        m_messageDelegate->setSenderAvatarPixmap(normalizedUrl, pixmap);
        if (!normalizedCacheKey.isEmpty()) {
            m_messageDelegate->setSenderAvatarPixmap(normalizedCacheKey, pixmap);
        }
        if (m_messageListView && m_messageListView->viewport()) {
            m_messageListView->viewport()->update();
        }
    });
}

void MainWindow::scheduleTypingStateUpdate()
{
    if (!m_messageInput) {
        return;
    }

    const QString conversationId = currentRoomId().trimmed();
    if (conversationId.isEmpty()) {
        return;
    }

    const bool shouldBeTyping = !m_messageInput->text().trimmed().isEmpty();
    if (shouldBeTyping) {
        sendTypingState(true);
        if (m_typingStopTimer) {
            m_typingStopTimer->start(kTypingStopDelayMs);
        }
    } else {
        sendTypingState(false);
    }
}

void MainWindow::sendTypingState(bool isTyping)
{
    const QString conversationId = currentRoomId().trimmed();
    if (conversationId.isEmpty()) {
        return;
    }

    m_profileManager->sendTypingState(conversationId, isTyping);
}

void MainWindow::clearTypingUsersForConversation(const QString &conversationId)
{
    m_profileManager->clearTypingUsersForConversation(conversationId);
}

void MainWindow::updateTypingStatusLabel()
{
    if (!m_typingStatusLabel) {
        return;
    }

    const QString conversationId = currentRoomId().trimmed();
    const QHash<QString, QString> typingUsers = m_typingUsersByConversationId.value(conversationId);
    if (typingUsers.isEmpty()) {
        m_typingStatusLabel->clear();
        m_typingStatusLabel->setVisible(false);
        return;
    }

    QStringList names;
    for (auto it = typingUsers.constBegin(); it != typingUsers.constEnd(); ++it) {
        names.append(it.value().isEmpty() ? it.key() : it.value());
    }
    names.removeAll(QString());
    if (names.isEmpty()) {
        m_typingStatusLabel->clear();
        m_typingStatusLabel->setVisible(false);
        return;
    }

    names.sort();
    QString text;
    if (names.size() == 1) {
        text = QStringLiteral("%1 正在输入...").arg(names.first());
    } else if (names.size() == 2) {
        text = QStringLiteral("%1、%2 正在输入...").arg(names.at(0), names.at(1));
    } else {
        text = QStringLiteral("%1 等 %2 人正在输入...").arg(names.first()).arg(names.size());
    }

    m_typingStatusLabel->setText(text);
    m_typingStatusLabel->setVisible(true);
}

void MainWindow::showFavoriteMessagesDialog()
{
    if (m_favoriteMessagesByKey.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("收藏夹"), QStringLiteral("当前没有收藏消息。"));
        return;
    }

    QList<QJsonObject> items = m_favoriteMessagesByKey.values();
    std::sort(items.begin(), items.end(), [](const QJsonObject &a, const QJsonObject &b) {
        return a.value(QStringLiteral("favoritedAt")).toString().toLongLong()
            > b.value(QStringLiteral("favoritedAt")).toString().toLongLong();
    });

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("收藏夹"));
    dialog.resize(680, 460);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *summaryLabel = new QLabel(QStringLiteral("已收藏 %1 条消息").arg(items.size()), &dialog);
    layout->addWidget(summaryLabel);

    auto *listWidget = new QListWidget(&dialog);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    listWidget->setWordWrap(true);
    for (const QJsonObject &item : items) {
        const QString conversationId = item.value(QStringLiteral("conversationId")).toString();
        const QString conversationName = item.value(QStringLiteral("conversationName")).toString();
        const qint64 messageId = item.value(QStringLiteral("serverMessageId")).toInteger(0);
        const QString senderId = item.value(QStringLiteral("senderId")).toString();
        const QString content = item.value(QStringLiteral("content")).toString();
        const qint64 timestampMs = item.value(QStringLiteral("timestamp")).toString().toLongLong();
        const QString timeText = ChatUtils::formatMessageTimeOrFallback(timestampMs, UiText::MainWindow::kUnknownTime);

        auto *listItem = new QListWidgetItem(
            QStringLiteral("[%1] %2: %3\n%4")
                .arg(conversationName.isEmpty() ? conversationId : conversationName,
                     senderId,
                     content,
                     timeText),
            listWidget);
        listItem->setData(Qt::UserRole, conversationId);
        listItem->setData(Qt::UserRole + 1, messageId);
    }
    layout->addWidget(listWidget, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    QPushButton *locateButton = buttonBox->addButton(QStringLiteral("定位消息"), QDialogButtonBox::ActionRole);
    QPushButton *removeButton = buttonBox->addButton(QStringLiteral("移除收藏"), QDialogButtonBox::DestructiveRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    locateButton->setEnabled(false);
    removeButton->setEnabled(false);

    connect(listWidget, &QListWidget::itemSelectionChanged, &dialog, [listWidget, locateButton, removeButton]() {
        const bool hasSelection = listWidget->currentItem() != nullptr;
        locateButton->setEnabled(hasSelection);
        removeButton->setEnabled(hasSelection);
    });

    connect(removeButton, &QPushButton::clicked, &dialog, [this, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) {
            return;
        }

        const QString conversationId = item->data(Qt::UserRole).toString();
        const qint64 messageId = item->data(Qt::UserRole + 1).toLongLong();
        const QString key = favoriteMessageKey(conversationId, messageId);
        if (!m_favoriteMessagesByKey.contains(key)) {
            return;
        }

        m_favoriteMessagesByKey.remove(key);
        persistFavoritesToSettings();
        m_messageHandler->refreshFavoriteHighlights();
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(locateButton, &QPushButton::clicked, &dialog, [this, listWidget, &dialog]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) {
            return;
        }

        const QString conversationId = item->data(Qt::UserRole).toString().trimmed();
        const qint64 messageId = item->data(Qt::UserRole + 1).toLongLong();
        if (conversationId.isEmpty() || messageId <= 0) {
            return;
        }

        if (!selectConversationById(conversationId)) {
            loadConversationData();
            return;
        }

        const int index = m_chatStore->currentConversationIndex();
        if (index >= 0) {
            m_pendingMessageFocusByConversationId.insert(conversationId, messageId);
            loadMessagesForConversation(index, messageId + 1, false);
            dialog.accept();
        }
    });

    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog.exec();
}

void MainWindow::loadFavoritesFromSettings()
{
    m_messageHandler->loadFavoritesFromSettings();
}

void MainWindow::persistFavoritesToSettings() const
{
    m_messageHandler->persistFavoritesToSettings();
}

void MainWindow::refreshNetworkUi()
{
    setNetworkStatus(UiText::MainWindow::kStatusPattern.arg(m_chatClient->statusText()));
    m_serverUrlInput->setEnabled(!m_chatClient->isConnected() && m_chatClient->isAvailable());
    m_connectButton->setEnabled(m_chatClient->isAvailable());
    m_connectButton->setText(m_chatClient->isConnected() ? UiText::MainWindow::kDisconnect
                                                         : UiText::MainWindow::kConnect);
}

void MainWindow::handleRawMessageReceived(const QString &messageText)
{
    setNetworkStatus(UiText::MainWindow::kStatusRealtimeMessage, messageText);
}

void MainWindow::showQueuedMessagesDialog()
{
    if (!m_chatStore) {
        return;
    }

    struct QueuedMessageItem
    {
        QString conversationId;
        QString conversationName;
        QString clientMessageId;
        QString content;
        qint64 timestamp = 0;
    };

    const auto collectQueuedMessages = [this]() {
        QList<QueuedMessageItem> queuedMessages;
        for (const Conversation &conversation : m_chatStore->conversations()) {
            const QString conversationName =
                conversation.name.trimmed().isEmpty() ? conversation.id : conversation.name;
            for (const Message &message : conversation.messages) {
                if (!message.isSelf || message.status != Message::DeliveryStatus::Queued) {
                    continue;
                }

                queuedMessages.append(QueuedMessageItem { conversation.id,
                                                          conversationName,
                                                          message.clientMessageId,
                                                          message.content,
                                                          message.timestamp });
            }
        }

        std::sort(queuedMessages.begin(), queuedMessages.end(),
                  [](const QueuedMessageItem &left, const QueuedMessageItem &right) {
                      return left.timestamp > right.timestamp;
                  });
        return queuedMessages;
    };

    if (collectQueuedMessages().isEmpty()) {
        QMessageBox::information(this,
                                 UiText::MainWindow::kQueueDetailsDialogTitle,
                                 UiText::MainWindow::kNoQueuedMessages);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(UiText::MainWindow::kQueueDetailsDialogTitle);
    dialog.resize(560, 380);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(14, 14, 14, 14);
    layout->setSpacing(10);

    auto *summaryLabel = new QLabel(&dialog);
    summaryLabel->setWordWrap(true);
    layout->addWidget(summaryLabel);

    auto *listWidget = new QListWidget(&dialog);
    listWidget->setWordWrap(true);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    QPushButton *resendAllButton = buttonBox->addButton(QStringLiteral("重发全部"), QDialogButtonBox::ActionRole);
    QPushButton *removeSelectedButton =
        buttonBox->addButton(QStringLiteral("删除选中"), QDialogButtonBox::DestructiveRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    const auto refreshList =
        [this, collectQueuedMessages, summaryLabel, listWidget, resendAllButton, removeSelectedButton]() {
        const QList<QueuedMessageItem> queuedMessages = collectQueuedMessages();
        listWidget->clear();
        for (const QueuedMessageItem &item : queuedMessages) {
            auto *listItem = new QListWidgetItem(
                UiText::MainWindow::kQueuedMessageItemPattern.arg(
                    ChatUtils::formatMessageTimeOrFallback(item.timestamp, UiText::MainWindow::kUnknownTime),
                    item.conversationName,
                    item.content),
                listWidget);
            listItem->setData(Qt::UserRole, item.conversationId);
            listItem->setData(Qt::UserRole + 1, item.clientMessageId);
        }

        summaryLabel->setText(
            UiText::MainWindow::kQueuedMessagesPattern.arg(QString::number(queuedMessages.size())));
        resendAllButton->setEnabled(!queuedMessages.isEmpty());
        removeSelectedButton->setEnabled(!queuedMessages.isEmpty());
        setNetworkStatus(UiText::MainWindow::kStatusPattern.arg(m_chatClient->statusText()));
    };

    refreshList();

    connect(resendAllButton, &QPushButton::clicked, &dialog, [this, refreshList, collectQueuedMessages]() {
        const QList<QueuedMessageItem> queuedMessages = collectQueuedMessages();

        if (queuedMessages.isEmpty()) {
            QMessageBox::information(this,
                                     UiText::MainWindow::kQueueDetailsDialogTitle,
                                     UiText::MainWindow::kNoResendableQueuedMessages);
            refreshList();
            return;
        }

        const bool connected = m_chatClient->isConnected();
        for (const QueuedMessageItem &item : queuedMessages) {
            if (item.conversationId.trimmed().isEmpty() || item.clientMessageId.trimmed().isEmpty()
                || item.content.trimmed().isEmpty()) {
                continue;
            }

            m_pendingMessageConversationIds.insert(item.clientMessageId, item.conversationId);
            if (connected && m_chatStore->markMessageSending(item.conversationId, item.clientMessageId)) {
                m_messageHandler->restartPendingMessageTimeout(item.conversationId, item.clientMessageId);
            }

            m_chatClient->sendChatMessage(item.content, item.conversationId, item.clientMessageId);
        }

        if (!connected) {
            m_chatClient->connectToServer(QUrl::fromUserInput(m_serverUrlInput->text().trimmed()), m_authToken);
            setNetworkStatus(UiText::MainWindow::kStatusConnectRealtimeFirst,
                             UiText::MainWindow::kStatusMessageQueuedDetail);
        } else {
            setNetworkStatus(UiText::MainWindow::kStatusResendQueueTriggered,
                             UiText::MainWindow::kResentQueuedMessagesPattern.arg(
                                 QString::number(queuedMessages.size())));
        }

        refreshList();
    });

    connect(removeSelectedButton, &QPushButton::clicked, &dialog, [this, listWidget, refreshList]() {
        QListWidgetItem *selectedItem = listWidget->currentItem();
        if (!selectedItem) {
            QMessageBox::information(this,
                                     UiText::MainWindow::kQueueDetailsDialogTitle,
                                     UiText::MainWindow::kSelectQueuedMessageFirst);
            return;
        }

        const QString conversationId = selectedItem->data(Qt::UserRole).toString();
        const QString clientMessageId = selectedItem->data(Qt::UserRole + 1).toString();
        if (conversationId.trimmed().isEmpty() || clientMessageId.trimmed().isEmpty()) {
            refreshList();
            return;
        }

        m_messageHandler->clearPendingMessageTimeout(clientMessageId);
        m_pendingMessageConversationIds.remove(clientMessageId);
        if (!m_chatStore->removeQueuedMessage(conversationId, clientMessageId)) {
            QMessageBox::warning(this,
                                 UiText::MainWindow::kQueueDetailsDialogTitle,
                                 UiText::MainWindow::kRemoveQueuedMessageFailed);
        }

        refreshList();
    });

    dialog.exec();
}

void MainWindow::handleJsonMessageReceived(const QJsonObject &payload)
{
    m_messageHandler->handleJsonMessageReceived(payload);
}

void MainWindow::scrollMessagesToBottom()
{
    QTimer::singleShot(0, this, [this]() {
        if (m_messageListView) {
            m_messageListView->scrollToBottom();
        }
    });
}

void MainWindow::setupUi()
{
    resize(1220, 780);
    setWindowTitle(UiText::MainWindow::kWindowTitle);

    auto *centralWidget = new QWidget(this);
    centralWidget->setObjectName(QStringLiteral("centralWidget"));
    setCentralWidget(centralWidget);

    auto *rootLayout = new QHBoxLayout(centralWidget);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(18);

    auto *sidebarFrame = new QFrame(centralWidget);
    sidebarFrame->setObjectName(QStringLiteral("sidebarFrame"));
    sidebarFrame->setMinimumWidth(270);
    sidebarFrame->setMaximumWidth(320);

    auto *sidebarLayout = new QVBoxLayout(sidebarFrame);
    sidebarLayout->setContentsMargins(18, 18, 18, 18);
    sidebarLayout->setSpacing(10);

    auto *sidebarHeaderLayout = new QHBoxLayout();
    sidebarHeaderLayout->setContentsMargins(0, 0, 0, 0);
    sidebarHeaderLayout->setSpacing(10);

    auto *sidebarTitle = new QLabel(UiText::MainWindow::kSidebarTitle, sidebarFrame);
    sidebarTitle->setObjectName(QStringLiteral("sidebarTitle"));

    m_profileAvatarLabel = new QLabel(sidebarFrame);
    m_profileAvatarLabel->setObjectName(QStringLiteral("profileAvatarLabel"));
    m_profileAvatarLabel->setAlignment(Qt::AlignCenter);
    m_profileAvatarLabel->setFixedSize(42, 42);
    m_profileAvatarLabel->setCursor(Qt::PointingHandCursor);
    m_profileAvatarLabel->installEventFilter(this);

    m_newConversationButton = new QPushButton(UiText::MainWindow::kNewDirectButton, sidebarFrame);
    m_newConversationButton->setObjectName(QStringLiteral("newConversationButton"));
    m_newConversationButton->setCursor(Qt::PointingHandCursor);
    m_newGroupButton = new QPushButton(UiText::MainWindow::kNewGroupButton, sidebarFrame);
    m_newGroupButton->setObjectName(QStringLiteral("newGroupButton"));
    m_newGroupButton->setCursor(Qt::PointingHandCursor);

    m_profileButton = new QPushButton(UiText::MainWindow::kDialogProfile, sidebarFrame);
    m_profileButton->setObjectName(QStringLiteral("profileButton"));
    m_profileButton->setCursor(Qt::PointingHandCursor);

    m_logoutButton = new QPushButton(UiText::MainWindow::kLogoutButton, sidebarFrame);
    m_logoutButton->setObjectName(QStringLiteral("logoutButton"));
    m_logoutButton->setCursor(Qt::PointingHandCursor);

    auto *conversationActionLayout = new QHBoxLayout();
    conversationActionLayout->setContentsMargins(0, 0, 0, 0);
    conversationActionLayout->setSpacing(8);
    conversationActionLayout->addWidget(m_profileButton, 1);
    conversationActionLayout->addWidget(m_newConversationButton, 1);
    conversationActionLayout->addWidget(m_newGroupButton, 1);
    conversationActionLayout->addWidget(m_logoutButton, 1);

    sidebarHeaderLayout->addWidget(m_profileAvatarLabel);
    sidebarHeaderLayout->addWidget(sidebarTitle);
    sidebarHeaderLayout->addStretch(1);

    auto *socialRowLayout = new QHBoxLayout();
    socialRowLayout->setContentsMargins(0, 0, 0, 0);
    socialRowLayout->setSpacing(8);

    m_friendsButton = new QPushButton(UiText::MainWindow::kFriendsButton, sidebarFrame);
    m_friendsButton->setObjectName(QStringLiteral("friendsButton"));
    m_friendsButton->setCursor(Qt::PointingHandCursor);

    m_friendRequestsButton = new QPushButton(UiText::MainWindow::kRequestsButton, sidebarFrame);
    m_friendRequestsButton->setObjectName(QStringLiteral("friendRequestsButton"));
    m_friendRequestsButton->setCursor(Qt::PointingHandCursor);

    m_blacklistButton = new QPushButton(UiText::MainWindow::kBlacklistButton, sidebarFrame);
    m_blacklistButton->setObjectName(QStringLiteral("blacklistButton"));
    m_blacklistButton->setCursor(Qt::PointingHandCursor);

    socialRowLayout->addWidget(m_friendsButton);
    socialRowLayout->addWidget(m_friendRequestsButton);
    socialRowLayout->addWidget(m_blacklistButton);

    auto *sidebarHint = new QLabel(
        UiText::MainWindow::kSidebarHint,
        sidebarFrame);
    sidebarHint->setObjectName(QStringLiteral("sidebarHint"));
    sidebarHint->setWordWrap(true);

    m_conversationListView = new QListView(sidebarFrame);
    m_conversationListView->setObjectName(QStringLiteral("conversationListView"));
    m_conversationListView->setModel(m_conversationModel);
    m_conversationListView->setItemDelegate(m_conversationDelegate);
    m_conversationListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_conversationListView->setSelectionMode(QAbstractItemView::SingleSelection);
    m_conversationListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_conversationListView->setSpacing(6);
    m_conversationListView->setUniformItemSizes(true);

    sidebarLayout->addLayout(sidebarHeaderLayout);
    sidebarLayout->addLayout(conversationActionLayout);
    sidebarLayout->addLayout(socialRowLayout);
    sidebarLayout->addWidget(sidebarHint);
    sidebarLayout->addWidget(m_conversationListView, 1);

    auto *chatFrame = new QFrame(centralWidget);
    chatFrame->setObjectName(QStringLiteral("chatFrame"));

    auto *chatLayout = new QVBoxLayout(chatFrame);
    chatLayout->setContentsMargins(22, 22, 22, 22);
    chatLayout->setSpacing(16);

    auto *headerFrame = new QFrame(chatFrame);
    headerFrame->setObjectName(QStringLiteral("headerFrame"));
    auto *headerLayout = new QVBoxLayout(headerFrame);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(10);

    m_conversationTitleLabel = new QLabel(headerFrame);
    m_conversationTitleLabel->setObjectName(QStringLiteral("conversationTitleLabel"));

    m_conversationMetaLabel = new QLabel(headerFrame);
    m_conversationMetaLabel->setObjectName(QStringLiteral("conversationMetaLabel"));
    m_typingStatusLabel = new QLabel(headerFrame);
    m_typingStatusLabel->setObjectName(QStringLiteral("conversationTypingLabel"));
    m_typingStatusLabel->setVisible(false);
    m_conversationMembersLabel = new QLabel(headerFrame);
    m_conversationMembersLabel->setObjectName(QStringLiteral("conversationMembersLabel"));
    m_conversationMembersLabel->setWordWrap(true);
    m_showMembersButton = new QPushButton(UiText::MainWindow::kMembersButton, headerFrame);
    m_showMembersButton->setObjectName(QStringLiteral("showMembersButton"));
    m_showMembersButton->setCursor(Qt::PointingHandCursor);
    m_inviteMembersButton = new QPushButton(UiText::MainWindow::kInviteButton, headerFrame);
    m_inviteMembersButton->setObjectName(QStringLiteral("inviteMembersButton"));
    m_inviteMembersButton->setCursor(Qt::PointingHandCursor);
    m_removeMemberButton = new QPushButton(UiText::MainWindow::kDialogRemoveMember, headerFrame);
    m_removeMemberButton->setObjectName(QStringLiteral("removeMemberButton"));
    m_removeMemberButton->setCursor(Qt::PointingHandCursor);
    m_leaveConversationButton = new QPushButton(UiText::MainWindow::kLeaveButton, headerFrame);
    m_leaveConversationButton->setObjectName(QStringLiteral("leaveConversationButton"));
    m_leaveConversationButton->setCursor(Qt::PointingHandCursor);

    auto *membersRow = new QHBoxLayout();
    membersRow->setContentsMargins(0, 0, 0, 0);
    membersRow->setSpacing(10);
    membersRow->addWidget(m_conversationMembersLabel, 1);
    membersRow->addWidget(m_showMembersButton);
    membersRow->addWidget(m_inviteMembersButton);
    membersRow->addWidget(m_removeMemberButton);
    membersRow->addWidget(m_leaveConversationButton);

    auto *networkFrame = new QFrame(headerFrame);
    networkFrame->setObjectName(QStringLiteral("networkFrame"));
    auto *networkLayout = new QHBoxLayout(networkFrame);
    networkLayout->setContentsMargins(0, 0, 0, 0);
    networkLayout->setSpacing(10);

    m_networkStatusLabel = new QLabel(networkFrame);
    m_networkStatusLabel->setObjectName(QStringLiteral("networkStatusLabel"));
    m_networkStatusLabel->setFixedWidth(260);

    m_showQueueDetailsButton = new QPushButton(UiText::MainWindow::kQueueDetailsButton, networkFrame);
    m_showQueueDetailsButton->setObjectName(QStringLiteral("queueDetailsButton"));
    m_showQueueDetailsButton->setCursor(Qt::PointingHandCursor);
    m_showQueueDetailsButton->setMinimumWidth(116);

    m_serverUrlInput = new QLineEdit(networkFrame);
    m_serverUrlInput->setObjectName(QStringLiteral("serverUrlInput"));
    m_serverUrlInput->setText(QStringLiteral("ws://127.0.0.1:3000/ws"));

    m_connectButton = new QPushButton(UiText::MainWindow::kConnect, networkFrame);
    m_connectButton->setObjectName(QStringLiteral("connectButton"));
    m_connectButton->setCursor(Qt::PointingHandCursor);
    m_connectButton->setFixedWidth(132);

    networkLayout->addWidget(m_networkStatusLabel, 1);
    networkLayout->addWidget(m_showQueueDetailsButton);
    networkLayout->addWidget(m_serverUrlInput, 1);
    networkLayout->addWidget(m_connectButton);

    headerLayout->addWidget(m_conversationTitleLabel);
    headerLayout->addWidget(m_conversationMetaLabel);
    headerLayout->addWidget(m_typingStatusLabel);
    headerLayout->addLayout(membersRow);
    headerLayout->addWidget(networkFrame);

    auto *searchFrame = new QFrame(chatFrame);
    searchFrame->setObjectName(QStringLiteral("searchFrame"));
    auto *searchLayout = new QHBoxLayout(searchFrame);
    searchLayout->setContentsMargins(0, 0, 0, 0);
    searchLayout->setSpacing(10);

    m_messageSearchInput = new QLineEdit(searchFrame);
    m_messageSearchInput->setObjectName(QStringLiteral("messageSearchInput"));
    m_messageSearchInput->setPlaceholderText(UiText::MainWindow::kSearchCurrentMessages);
    m_messageSearchInput->setClearButtonEnabled(true);

    m_messageSearchResultLabel = new QLabel(QStringLiteral("0/0"), searchFrame);
    m_messageSearchResultLabel->setObjectName(QStringLiteral("messageSearchResultLabel"));
    m_messageSearchResultLabel->setMinimumWidth(56);

    m_messageSearchPrevButton = new QPushButton(UiText::MainWindow::kPrevButton, searchFrame);
    m_messageSearchPrevButton->setObjectName(QStringLiteral("searchPrevButton"));
    m_messageSearchPrevButton->setCursor(Qt::PointingHandCursor);

    m_messageSearchNextButton = new QPushButton(UiText::MainWindow::kNextButton, searchFrame);
    m_messageSearchNextButton->setObjectName(QStringLiteral("searchNextButton"));
    m_messageSearchNextButton->setCursor(Qt::PointingHandCursor);

    m_globalSearchButton = new QPushButton(UiText::MainWindow::kGlobalSearchButton, searchFrame);
    m_globalSearchButton->setObjectName(QStringLiteral("globalSearchButton"));
    m_globalSearchButton->setCursor(Qt::PointingHandCursor);

    m_favoriteMessagesButton = new QPushButton(UiText::MainWindow::kFavoritesButton, searchFrame);
    m_favoriteMessagesButton->setObjectName(QStringLiteral("favoritesButton"));
    m_favoriteMessagesButton->setCursor(Qt::PointingHandCursor);

    searchLayout->addWidget(m_messageSearchInput, 1);
    searchLayout->addWidget(m_messageSearchResultLabel);
    searchLayout->addWidget(m_messageSearchPrevButton);
    searchLayout->addWidget(m_messageSearchNextButton);
    searchLayout->addWidget(m_globalSearchButton);
    searchLayout->addWidget(m_favoriteMessagesButton);

    m_messageListView = new QListView(chatFrame);
    m_messageListView->setObjectName(QStringLiteral("messageListView"));
    m_messageListView->setModel(m_messageModel);
    m_messageListView->setItemDelegate(m_messageDelegate);
    m_messageListView->setSelectionMode(QAbstractItemView::NoSelection);
    m_messageListView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_messageListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_messageListView->setSpacing(2);

    auto *composerFrame = new QFrame(chatFrame);
    composerFrame->setObjectName(QStringLiteral("composerFrame"));
    auto *composerLayout = new QHBoxLayout(composerFrame);
    composerLayout->setContentsMargins(0, 0, 0, 0);
    composerLayout->setSpacing(12);

    m_messageInput = new QLineEdit(composerFrame);
    m_messageInput->setObjectName(QStringLiteral("messageInput"));
    m_messageInput->setPlaceholderText(
        UiText::MainWindow::kMessageInputPlaceholder);
    m_messageInput->setClearButtonEnabled(true);

    m_sendFileButton = new QPushButton(UiText::MainWindow::kSendFileButton, composerFrame);
    m_sendFileButton->setObjectName(QStringLiteral("sendFileButton"));
    m_sendFileButton->setCursor(Qt::PointingHandCursor);

    m_sendButton = new QPushButton(UiText::MainWindow::kSendButton, composerFrame);
    m_sendButton->setObjectName(QStringLiteral("sendButton"));
    m_sendButton->setCursor(Qt::PointingHandCursor);

    m_emojiButton = new QPushButton(QStringLiteral("😀"), composerFrame);
    m_emojiButton->setObjectName(QStringLiteral("sendFileButton"));
    m_emojiButton->setCursor(Qt::PointingHandCursor);
    m_emojiButton->setFixedSize(46, 46);
    m_emojiButton->setStyleSheet(
        QStringLiteral("QPushButton { font-size: 20px; border: none; border-radius: 18px; background: #f3f4f6; }"
                        "QPushButton:hover { background: #e5e7eb; }"));

    m_emojiPicker = new EmojiPicker(this);

    m_replyBar = new QFrame(chatFrame);
    m_replyBar->setObjectName(QStringLiteral("headerFrame"));
    m_replyBar->setVisible(false);
    auto *replyLayout = new QHBoxLayout(m_replyBar);
    replyLayout->setContentsMargins(8, 6, 8, 6);
    replyLayout->setSpacing(8);

    auto *replyIcon = new QLabel(QStringLiteral("↩"), m_replyBar);
    replyIcon->setStyleSheet(QStringLiteral("color: #2563eb; font-size: 14px; font-weight: 700;"));
    m_replyPreviewLabel = new QLabel(m_replyBar);
    m_replyPreviewLabel->setStyleSheet(QStringLiteral("color: #374151; font-size: 12px;"));
    m_replyPreviewLabel->setWordWrap(true);
    m_replyCancelButton = new QPushButton(QStringLiteral("✕"), m_replyBar);
    m_replyCancelButton->setFixedSize(24, 24);
    m_replyCancelButton->setStyleSheet(
        QStringLiteral("QPushButton { border: none; color: #9ca3af; font-size: 14px; }"
                        "QPushButton:hover { color: #ef4444; }"));
    m_replyCancelButton->setCursor(Qt::PointingHandCursor);

    replyLayout->addWidget(replyIcon);
    replyLayout->addWidget(m_replyPreviewLabel, 1);
    replyLayout->addWidget(m_replyCancelButton);

    composerLayout->addWidget(m_emojiButton);
    composerLayout->addWidget(m_messageInput, 1);
    composerLayout->addWidget(m_sendFileButton);
    composerLayout->addWidget(m_sendButton);

    chatLayout->addWidget(headerFrame);
    chatLayout->addWidget(searchFrame);
    chatLayout->addWidget(m_replyBar);
    chatLayout->addWidget(m_messageListView, 1);
    chatLayout->addWidget(composerFrame);

    rootLayout->addWidget(sidebarFrame);
    rootLayout->addWidget(chatFrame, 1);

    centralWidget->setStyleSheet(styleSheet());

    updateMessageSearchUi();
}

QString MainWindow::styleSheet()
{
    const QString base =
        "QWidget#centralWidget { background: #eef3f8; }"
        "QFrame#sidebarFrame, QFrame#chatFrame { background: white; border-radius: 24px; }";

    const QString labels =
        "QLabel#sidebarTitle, QLabel#conversationTitleLabel { color: #111827; font-size: 22px; font-weight: 700; }"
        "QLabel#profileAvatarLabel { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #60A5FA, stop:1 #2563EB); color: white; border-radius: 21px; font-size: 16px; font-weight: 800; }"
        "QLabel#profileAvatarPreviewLabel { background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #60A5FA, stop:1 #2563EB); color: white; border-radius: 44px; font-size: 28px; font-weight: 900; }"
        "QLabel#sidebarHint, QLabel#conversationMetaLabel, QLabel#conversationMembersLabel, QLabel#networkStatusLabel { color: #6b7280; font-size: 13px; }"
        "QLabel#conversationTypingLabel { color: #0ea5a5; font-size: 12px; font-weight: 600; }"
        "QLabel#messageSearchResultLabel { color: #64748B; font-size: 12px; font-weight: 600; }";

    const QString lists =
        "QListView#conversationListView { border: none; background: transparent; outline: none; }"
        "QListView#conversationListView::item { background: #f8fafc; border-radius: 16px; margin: 2px 0; }"
        "QListView#conversationListView::item:selected { background: #dbeafe; color: #0f172a; }"
        "QListView#messageListView { border: none; background: #f8fafc; outline: none; padding: 10px 0; }";

    const QString inputs =
        "QLineEdit#messageInput, QLineEdit#serverUrlInput, QLineEdit#messageSearchInput { min-height: 46px; border: 1px solid #d1d5db; border-radius: 18px; padding: 0 16px; background: #ffffff; color: #111827; }"
        "QLineEdit#messageInput:focus, QLineEdit#serverUrlInput:focus, QLineEdit#messageSearchInput:focus { border: 1px solid #60a5fa; }";

    const QString allButtons =
        "QPushButton#sendButton, QPushButton#sendFileButton, QPushButton#connectButton,"
        "QPushButton#profileButton, QPushButton#newConversationButton, QPushButton#newGroupButton,"
        "QPushButton#friendsButton, QPushButton#friendRequestsButton, QPushButton#blacklistButton,"
        "QPushButton#showMembersButton, QPushButton#inviteMembersButton, QPushButton#removeMemberButton,"
        "QPushButton#leaveConversationButton, QPushButton#queueDetailsButton,"
        "QPushButton#searchPrevButton, QPushButton#searchNextButton, QPushButton#globalSearchButton,"
        "QPushButton#favoritesButton, QPushButton#logoutButton"
        " { min-width: 108px; min-height: 46px; border: none; border-radius: 18px; background: #2563eb; color: white; font-weight: 700; }";

    const QString buttonOverrides =
        "QPushButton#sendFileButton { background: #0EA5E9; min-width: 108px; }"
        "QPushButton#profileButton, QPushButton#newConversationButton, QPushButton#newGroupButton,"
        "QPushButton#friendsButton, QPushButton#friendRequestsButton, QPushButton#blacklistButton,"
        "QPushButton#showMembersButton, QPushButton#inviteMembersButton, QPushButton#removeMemberButton,"
        "QPushButton#leaveConversationButton, QPushButton#queueDetailsButton,"
        "QPushButton#searchPrevButton, QPushButton#searchNextButton, QPushButton#globalSearchButton,"
        "QPushButton#favoritesButton { min-width: 96px; min-height: 40px; border-radius: 14px; }"
        "QPushButton#profileButton, QPushButton#newConversationButton, QPushButton#newGroupButton, QPushButton#logoutButton { min-width: 0px; min-height: 38px; padding-left: 8px; padding-right: 8px; border-radius: 13px; }"
        "QPushButton#profileButton { background: #64748B; }"
        "QPushButton#logoutButton { background: #EF4444; }"
        "QPushButton#friendsButton, QPushButton#friendRequestsButton, QPushButton#blacklistButton { background: #14B8A6; min-width: 72px; }"
        "QPushButton#searchPrevButton, QPushButton#searchNextButton, QPushButton#globalSearchButton, QPushButton#favoritesButton { background: #0EA5E9; }";

    const QString buttonStates =
        "QPushButton:hover { background: #1d4ed8; }"
        "QPushButton#friendsButton:hover, QPushButton#friendRequestsButton:hover, QPushButton#blacklistButton:hover { background: #0F766E; }"
        "QPushButton:disabled { background: #93c5fd; }";

    return base + labels + lists + inputs + allButtons + buttonOverrides + buttonStates;
}

void MainWindow::setupConnections()
{
    connect(m_sendButton, &QPushButton::clicked, this, &MainWindow::sendCurrentMessage);
    connect(m_sendFileButton, &QPushButton::clicked, this, &MainWindow::sendMediaFile);
    connect(m_emojiButton, &QPushButton::clicked, this, [this]() {
        m_emojiPicker->showAt(m_emojiButton->mapToGlobal(QPoint(0, -220)));
    });
    connect(m_emojiPicker, &EmojiPicker::emojiSelected, this, [this](const QString &emoji) {
        m_messageInput->insert(emoji);
        m_messageInput->setFocus();
    });
    connect(m_replyCancelButton, &QPushButton::clicked, this, &MainWindow::cancelReply);
    connect(m_messageInput, &QLineEdit::returnPressed, this, &MainWindow::sendCurrentMessage);
    connect(m_messageInput, &QLineEdit::textChanged, this, [this](const QString &text) {
        saveDraftForConversation(currentRoomId(), text);
    });
    connect(m_messageInput, &QLineEdit::textEdited, this, [this](const QString &) {
        if (m_suppressTypingSignals) {
            return;
        }
        scheduleTypingStateUpdate();
    });
    connect(m_messageSearchInput, &QLineEdit::textChanged, this, [this]() {
        refreshMessageSearchMatches(true);
    });
    connect(m_messageSearchPrevButton, &QPushButton::clicked, this, [this]() {
        jumpMessageSearchMatch(-1);
    });
    connect(m_messageSearchNextButton, &QPushButton::clicked, this, [this]() {
        jumpMessageSearchMatch(1);
    });
    connect(m_globalSearchButton, &QPushButton::clicked, this, &MainWindow::runGlobalMessageSearch);
    connect(m_favoriteMessagesButton, &QPushButton::clicked, this, &MainWindow::showFavoriteMessagesDialog);
    connect(m_connectButton, &QPushButton::clicked, this, &MainWindow::toggleConnection);
    connect(m_profileButton, &QPushButton::clicked, this, &MainWindow::showProfileDialog);
    connect(m_newConversationButton, &QPushButton::clicked, this, &MainWindow::createDirectConversation);
    connect(m_newGroupButton, &QPushButton::clicked, this, &MainWindow::createGroupConversation);
    connect(m_friendsButton, &QPushButton::clicked, this, &MainWindow::showFriendsDialog);
    connect(m_friendRequestsButton, &QPushButton::clicked, this, &MainWindow::showFriendRequestsDialog);
    connect(m_blacklistButton, &QPushButton::clicked, this, &MainWindow::showBlacklistDialog);
    connect(m_showMembersButton, &QPushButton::clicked, this, &MainWindow::showMembersDialog);
    connect(m_inviteMembersButton, &QPushButton::clicked, this, &MainWindow::inviteMembersToCurrentConversation);
    connect(m_removeMemberButton, &QPushButton::clicked, this, &MainWindow::removeMemberFromCurrentConversation);
    connect(m_leaveConversationButton, &QPushButton::clicked, this, &MainWindow::leaveCurrentConversation);
    connect(m_showQueueDetailsButton, &QPushButton::clicked, this, &MainWindow::showQueuedMessagesDialog);
    connect(m_logoutButton, &QPushButton::clicked, this, &MainWindow::logout);
    connect(m_messageListView, &QListView::doubleClicked, this, &MainWindow::handleMessageActivated);
    m_messageListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_messageListView, &QListView::customContextMenuRequested, this, [this](const QPoint &point) {
        if (!m_messageListView || !m_messageModel) {
            return;
        }

        const QModelIndex index = m_messageListView->indexAt(point);
        if (!index.isValid()) {
            return;
        }

        const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
        if (serverMessageId <= 0) {
            return;
        }

        const QString conversationId = currentRoomId();
        if (conversationId.trimmed().isEmpty()) {
            return;
        }

        QString mediaFileName;
        QString mediaUrl;
        const bool hasMediaLink = MediaUtils::parseMediaMessageContent(
            index.data(MessageListModel::ContentRole).toString(),
            nullptr,
            &mediaFileName,
            &mediaUrl);

        QMenu menu(this);
        QAction *openMediaAction = nullptr;
        if (hasMediaLink) {
            openMediaAction = menu.addAction(UiText::MainWindow::kOpenFile);
        }
        QAction *editAction = nullptr;
        if (index.data(MessageListModel::IsSelfRole).toBool() && serverMessageId > 0) {
            editAction = menu.addAction(UiText::MainWindow::kEditMessage);
        }
        QAction *recallAction = nullptr;
        if (canRecallMessageLocally(index)) {
            recallAction = menu.addAction(UiText::MainWindow::kRecallMessage);
        }
        QAction *toggleFavoriteAction = menu.addAction(
            isFavoriteMessage(conversationId, serverMessageId)
                ? UiText::MainWindow::kUnfavoriteMessage
                : UiText::MainWindow::kFavoriteMessage);
        QAction *replyAction = menu.addAction(QStringLiteral("回复"));
        QAction *copyAction = menu.addAction(UiText::MainWindow::kCopyMessage);

        QAction *selectedAction = menu.exec(m_messageListView->viewport()->mapToGlobal(point));
        if (selectedAction == openMediaAction) {
            openMediaLinkByIndex(index);
            return;
        }
        if (selectedAction == editAction) {
            editMessage(index);
            return;
        }
        if (selectedAction == recallAction) {
            recallMessageByIndex(index);
            return;
        }
        if (selectedAction == toggleFavoriteAction) {
            toggleFavoriteForMessageIndex(index);
            return;
        }
        if (selectedAction == replyAction) {
            const QString content = index.data(MessageListModel::ContentRole).toString();
            const QString senderId = index.data(MessageListModel::SenderIdRole).toString();
            startReply(serverMessageId, content, senderId);
            return;
        }
        if (selectedAction == copyAction) {
            const QString content = index.data(MessageListModel::ContentRole).toString();
            if (!content.isEmpty()) {
                QGuiApplication::clipboard()->setText(content);
            }
        }
    });
    m_conversationListView->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(m_conversationListView, &QListView::customContextMenuRequested, this, [this](const QPoint &point) {
        if (!m_conversationListView) {
            return;
        }

        const QModelIndex index = m_conversationListView->indexAt(point);
        if (!index.isValid()) {
            return;
        }

        const Conversation *conversation = m_chatStore->conversationAt(index.row());
        if (!conversation) {
            return;
        }

        QMenu menu(this);
        QAction *deleteAction = menu.addAction(UiText::MainWindow::kDeleteConversation);
        QAction *pinAction = menu.addAction(
            m_conversationManager->isConversationPinned(conversation->id)
                ? UiText::MainWindow::kUnpinConversation
                : UiText::MainWindow::kPinConversation);
        QAction *muteAction = menu.addAction(
            m_conversationManager->isConversationMuted(conversation->id)
                ? UiText::MainWindow::kUnmuteConversation
                : UiText::MainWindow::kMuteConversation);

        QAction *selectedAction = menu.exec(m_conversationListView->viewport()->mapToGlobal(point));
        if (selectedAction == deleteAction) {
            deleteCurrentConversation();
        } else if (selectedAction == pinAction) {
            togglePinCurrentConversation();
        } else if (selectedAction == muteAction) {
            toggleMuteCurrentConversation();
        }
    });
    connect(m_conversationListView->selectionModel(), &QItemSelectionModel::currentChanged, this,
            &MainWindow::onConversationSelected);
    connect(m_chatStore, &ChatStore::currentConversationChanged, this,
            &MainWindow::refreshConversationHeader);
    connect(m_chatStore, &ChatStore::conversationsReset, this, [this]() {
        if (m_databaseManager && m_databaseManager->isOpen()) {
            m_databaseManager->saveConversations(m_chatStore->conversations());
        }
        QTimer::singleShot(50, this, &MainWindow::scrollMessagesToBottom);
    });
    connect(m_chatStore, &ChatStore::messageAppended, this, [this](int index) {
        if (m_databaseManager && m_databaseManager->isOpen()) {
            const Conversation *conv = m_chatStore->currentConversation();
            const Message *msg = m_chatStore->messageAt(index);
            if (conv && msg) {
                m_databaseManager->appendMessage(conv->id, *msg);
            }
        }
    });
    connect(m_chatStore, &ChatStore::messageUpdated, this, [this](int index) {
        if (m_databaseManager && m_databaseManager->isOpen()) {
            const Conversation *conv = m_chatStore->currentConversation();
            const Message *msg = m_chatStore->messageAt(index);
            if (conv && msg) {
                if (msg->serverMessageId > 0) {
                    m_databaseManager->updateMessage(conv->id, msg->serverMessageId, *msg);
                } else if (!msg->clientMessageId.isEmpty()) {
                    m_databaseManager->appendMessage(conv->id, *msg);
                }
            }
        }
    });
    connect(m_chatStore, &ChatStore::conversationUpdated, this, [this](int row) {
        if (row == m_chatStore->currentConversationIndex()) {
            refreshConversationHeader();
        }
    });
    connect(m_chatStore, &ChatStore::currentConversationChanged, this,
            &MainWindow::scrollMessagesToBottom);
    connect(m_messageModel, &QAbstractItemModel::rowsInserted, this,
            [this](const QModelIndex &parent, int first, int last) {
                Q_UNUSED(parent);
                Q_UNUSED(last);
                if (first == 0 && m_chatStore->currentMessageCount() > 1) {
                    return;
                }
                QTimer::singleShot(50, this, &MainWindow::scrollMessagesToBottom);
                refreshMessageSearchMatches(false);
            });
    connect(m_messageModel, &QAbstractItemModel::modelReset, this, [this]() {
        refreshMessageSearchMatches(false);
        m_messageHandler->refreshFavoriteHighlights();
        preloadMediaThumbnailsForCurrentConversation();
        preloadMessageAvatarsForCurrentConversation();
    });
    connect(m_messageModel, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex &, const QModelIndex &) {
                refreshMessageSearchMatches(false);
                m_messageHandler->refreshFavoriteHighlights();
            });
    if (m_messageListView && m_messageListView->verticalScrollBar()) {
        connect(m_messageListView->verticalScrollBar(), &QScrollBar::valueChanged, this, [this](int value) {
            if (value > kOlderMessagesLoadTrigger) {
                return;
            }

            const int currentIndex = m_chatStore->currentConversationIndex();
            const Conversation *conversation = m_chatStore->currentConversation();
            if (currentIndex < 0 || !conversation) {
                return;
            }

            MessagePaginationState &state = m_messagePaginationStates[conversation->id];
            if (state.isLoadingOlder || !state.hasMore || state.oldestServerMessageId <= 0) {
                return;
            }

            loadMessagesForConversation(currentIndex, state.oldestServerMessageId, true);
        });
    }
    connect(m_chatClient, &ChatClient::connectionStateChanged, this, &MainWindow::refreshNetworkUi);
    connect(m_chatClient, &ChatClient::connected, this, [this]() {
        m_messageHandler->resumeQueuedMessagesAfterReconnect();
        refreshNetworkUi();
        updateTypingStatusLabel();
    });
    connect(m_chatClient, &ChatClient::disconnected, this, [this]() {
        m_typingUsersByConversationId.clear();
        updateTypingStatusLabel();
        refreshNetworkUi();
    });
    connect(m_chatClient, &ChatClient::errorOccurred, this, [this](const QString &errorText) {
        setNetworkStatus(UiText::MainWindow::kStatusConnectionError, errorText);
    });
    connect(m_chatClient, &ChatClient::rawMessageReceived, this, &MainWindow::handleRawMessageReceived);
    connect(m_chatClient, &ChatClient::jsonMessageReceived, this, &MainWindow::handleJsonMessageReceived);

    // MessageHandler signal connections for UI reactions
    connect(m_messageHandler, &MessageHandler::networkStatusChanged, this, &MainWindow::setNetworkStatus);
    connect(m_messageHandler, &MessageHandler::scrollMessagesToBottom, this, &MainWindow::scrollMessagesToBottom);
    connect(m_messageHandler, &MessageHandler::messageReceived, this, [this](const QString &conversationId, const Message &message) {
        if (conversationId == currentRoomId()) {
            scrollMessagesToBottom();
            if (message.serverMessageId > 0) {
                bool isImage = false;
                QString fileName, rawUrl;
                if (MediaUtils::parseMediaMessageContent(message.content, &isImage, &fileName, &rawUrl) && isImage) {
                    requestMediaThumbnail(message.serverMessageId, rawUrl);
                }
                if (!message.senderAvatarUrl.isEmpty()) {
                    requestMessageAvatar(message.senderAvatarUrl, message.senderId);
                }
                markConversationReadOnServer(conversationId, message.serverMessageId);
            }
        }
        refreshNetworkUi();
    });
    connect(m_messageHandler, &MessageHandler::messageRecalled, this, [this](const QString &conversationId, qint64) {
        Q_UNUSED(conversationId);
    });
    connect(m_messageHandler, &MessageHandler::realtimeConnected, this, [this]() {
        m_messageHandler->joinCurrentRoomIfConnected();
    });
    connect(m_messageHandler, &MessageHandler::profileUpdatedFromServer, this,
        [this](const QString &email, const QString &nickname, const QString &avatarUrl) {
            if (email.compare(m_loggedInUserEmail, Qt::CaseInsensitive) != 0) {
                return;
            }
            m_profileManager->setCurrentUserNickname(nickname);
            m_profileManager->setCurrentUserAvatarUrl(avatarUrl);
            m_loggedInUserNickname = nickname;
            m_loggedInUserAvatarUrl = avatarUrl;
            QSettings settings;
            settings.setValue(QStringLiteral("auth/user_nickname"), nickname);
            settings.setValue(QStringLiteral("auth/user_avatar_url"), avatarUrl);
            settings.sync();
            updateProfileAvatarBadge();
            refreshConversationHeader();
        });
    connect(m_messageHandler, &MessageHandler::presenceUpdated, this,
        [this](const QString &conversationId, const QString &userEmail, bool isOnline, qint64 lastSeenAt) {
            if (conversationId == currentRoomId() && !userEmail.isEmpty()) {
                for (GroupMemberInfo &member : m_currentConversationMembers) {
                    if (member.email.compare(userEmail, Qt::CaseInsensitive) == 0) {
                        member.isOnline = isOnline;
                        if (lastSeenAt > 0) member.lastSeenAt = lastSeenAt;
                        break;
                    }
                }
                refreshConversationHeader();
            }
            if (m_presenceRefreshTimer && !m_presenceRefreshTimer->isActive()) {
                m_presenceRefreshTimer->start(350);
            }
        });
    connect(m_messageHandler, &MessageHandler::serverError, this,
        [this](const QString &clientMessageId, const QString &conversationId, const QString &message) {
            Q_UNUSED(clientMessageId);
            Q_UNUSED(conversationId);
            setNetworkStatus(UiText::MainWindow::kStatusServerError, message);
        });
    connect(m_messageHandler, &MessageHandler::focusMessageRequested, this,
        [this](const QString &conversationId, qint64 serverMessageId) {
            focusMessageByServerIdInConversation(conversationId, serverMessageId);
        });
    connect(m_messageHandler, &MessageHandler::favoriteHighlightsRefreshed, this, [this]() {
        if (m_messageDelegate && m_chatStore) {
            QSet<qint64> favoriteIdsForCurrentConversation;
            const QString conversationId = currentRoomId();
            const Conversation *conversation = m_chatStore->currentConversation();
            if (conversation && !conversationId.trimmed().isEmpty()) {
                for (const Message &message : conversation->messages) {
                    if (message.serverMessageId > 0 && m_messageHandler->isFavoriteMessage(conversationId, message.serverMessageId)) {
                        favoriteIdsForCurrentConversation.insert(message.serverMessageId);
                    }
                }
            }
            m_messageDelegate->setFavoriteServerMessageIds(favoriteIdsForCurrentConversation);
            if (m_messageListView && m_messageListView->viewport()) {
                m_messageListView->viewport()->update();
            }
        }
    });
    connect(m_messageHandler, &MessageHandler::typingUsersUpdated, this,
        [this](const QString &conversationId, const QHash<QString, QString> &users) {
            if (users.isEmpty()) {
                m_typingUsersByConversationId.remove(conversationId);
            } else {
                m_typingUsersByConversationId.insert(conversationId, users);
            }
            if (conversationId == currentRoomId()) {
                updateTypingStatusLabel();
            }
        });
    connect(m_profileManager, &ProfileManager::profileUpdated, this, [this]() {
        m_loggedInUserNickname = m_profileManager->currentUserNickname();
        m_loggedInUserAvatarUrl = m_profileManager->currentUserAvatarUrl();
        updateProfileAvatarBadge();
        refreshConversationHeader();
    });
    connect(m_profileManager, &ProfileManager::typingUsersUpdated, this,
        [this](const QString &conversationId, const QHash<QString, QString> &users) {
            if (users.isEmpty()) {
                m_typingUsersByConversationId.remove(conversationId);
            } else {
                m_typingUsersByConversationId.insert(conversationId, users);
            }
            if (conversationId == currentRoomId()) {
                updateTypingStatusLabel();
            }
        });
    connect(m_conversationManager, &ConversationManager::networkStatusChanged,
            this, &MainWindow::setNetworkStatus);
    connect(m_networkService, &NetworkService::networkStatusChanged,
            this, &MainWindow::setNetworkStatus);
}

void MainWindow::syncInitialSelection()
{
    if (m_conversationModel->rowCount() == 0) {
        return;
    }

    const QModelIndex firstConversation = m_conversationModel->index(0, 0);
    m_conversationListView->setCurrentIndex(firstConversation);
    m_conversationListView->selectionModel()->setCurrentIndex(
        firstConversation, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
    scrollMessagesToBottom();
}

void MainWindow::loadConversationData()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        m_typingUsersByConversationId.clear();
        updateTypingStatusLabel();
        m_loadingMediaThumbnailIds.clear();
        if (m_messageDelegate) {
            m_messageDelegate->clearMediaThumbnails();
        }
        m_chatStore->clear();
        setNetworkStatus(UiText::MainWindow::kStatusMissingSession);
        return;
    }

    m_conversationManager->loadConversations(m_backendBaseUrl, m_loggedInUserEmail);
}

void MainWindow::loadMessagesForConversation(int index, qint64 beforeId, bool prepend)
{
    const Conversation *conversation = m_chatStore->conversationAt(index);
    if (!conversation) {
        return;
    }

    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        return;
    }

    const QString conversationId = conversation->id;
    MessagePaginationState &paginationState = m_messagePaginationStates[conversationId];

    if (!prepend && m_databaseManager && m_databaseManager->isOpen()) {
        const QList<Message> cached = m_databaseManager->loadMessages(conversationId);
        if (!cached.isEmpty()) {
            m_chatStore->replaceMessagesForConversation(index, cached);
        }
    }

    if (prepend && paginationState.isLoadingOlder) {
        return;
    }
    paginationState.isLoadingOlder = true;
    if (prepend) {
        setNetworkStatus(UiText::MainWindow::kStatusLoadingOlderMessages, conversation->name);
    }
    if (!prepend) {
        paginationState.hasMore = true;
        paginationState.oldestServerMessageId = 0;
    }

    QUrl url = QUrl::fromUserInput(m_backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/messages").arg(conversationId));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("limit"), QString::number(kMessagePageSize));
    if (beforeId > 0) {
        query.addQueryItem(QStringLiteral("beforeId"), QString::number(beforeId));
    }
    url.setQuery(query);

    const int requestSerial = ++m_messageLoadSerial;
    const QScrollBar *scrollBar = m_messageListView ? m_messageListView->verticalScrollBar() : nullptr;
    const int previousScrollMax = prepend && scrollBar ? scrollBar->maximum() : -1;
    const int previousScrollValue = prepend && scrollBar ? scrollBar->value() : -1;

    m_networkService->getJsonAsync(url, [this, requestSerial, index, conversationId, prepend, previousScrollMax,
                       previousScrollValue](const NetworkService::HttpResult &result) {
        MessagePaginationState &state = m_messagePaginationStates[conversationId];
        state.isLoadingOlder = false;

        if (requestSerial != m_messageLoadSerial) {
            return;
        }

        const Conversation *latestConversation = m_chatStore->conversationAt(index);
        if (!latestConversation || latestConversation->id != conversationId) {
            return;
        }

        if (!result.ok) {
            if (prepend) {
                setNetworkStatus(UiText::MainWindow::kStatusLoadOlderMessagesFailed, result.message);
            } else {
                setNetworkStatus(UiText::MainWindow::kStatusLoadMessagesFailed, result.message);
            }
            return;
        }

        const QJsonArray items = result.body.value(QStringLiteral("messages")).toArray();
        QList<Message> messages;
        messages.reserve(items.size());
        for (const QJsonValue &value : items) {
            messages.append(ChatUtils::messageFromBackendPayload(value.toObject(), m_loggedInUserEmail));
        }

        const bool dataChanged = prepend
            ? m_chatStore->prependMessagesForConversation(index, std::move(messages))
            : m_chatStore->replaceMessagesForConversation(index, std::move(messages));

        if (dataChanged && m_databaseManager && m_databaseManager->isOpen()) {
            const Conversation *updatedConv = m_chatStore->conversationAt(index);
            if (updatedConv) {
                m_databaseManager->saveMessages(conversationId, updatedConv->messages);
            }
        }

        const QJsonObject pagination = result.body.value(QStringLiteral("pagination")).toObject();
        const bool protocolHasMore = pagination.value(QStringLiteral("hasMore")).toBool(items.size() >= kMessagePageSize);
        const qint64 protocolNextBeforeId = pagination.value(QStringLiteral("nextBeforeId")).toInteger(0);
        const qint64 serverLastReadMessageId =
            result.body.value(QStringLiteral("readState")).toObject().value(QStringLiteral("lastReadMessageId")).toInteger(0);
        const qint64 peerLastReadMessageId =
            result.body.value(QStringLiteral("readState")).toObject().value(QStringLiteral("peerLastReadMessageId")).toInteger(0);
        if (serverLastReadMessageId > 0) {
            const qint64 currentAck = m_lastReadAckMessageIds.value(conversationId, 0);
            if (serverLastReadMessageId > currentAck) {
                m_lastReadAckMessageIds.insert(conversationId, serverLastReadMessageId);
            }
        }
        if (peerLastReadMessageId > 0) {
            m_chatStore->markMessagesReadByPeer(conversationId, peerLastReadMessageId);
        }

        state.hasMore = protocolHasMore;
        state.oldestServerMessageId = protocolNextBeforeId;
        if (state.oldestServerMessageId <= 0) {
            const Conversation *updatedConversation = m_chatStore->conversationAt(index);
            state.oldestServerMessageId = oldestLoadedServerMessageId(updatedConversation);
        }
        if (state.oldestServerMessageId <= 0) {
            state.hasMore = false;
        }
        if (prepend) {
            if (!dataChanged || items.isEmpty()) {
                setNetworkStatus(UiText::MainWindow::kStatusNoOlderMessages, conversationId);
            } else if (!state.hasMore) {
                setNetworkStatus(UiText::MainWindow::kStatusOlderMessagesLoaded,
                                 UiText::MainWindow::kOlderMessagesReachedBeginning);
            } else {
                setNetworkStatus(UiText::MainWindow::kStatusOlderMessagesLoaded,
                                 UiText::MainWindow::kLoadedOlderMessagesPattern.arg(items.size()));
            }
        }

        if (prepend && dataChanged && previousScrollMax >= 0 && previousScrollValue >= 0 && m_messageListView) {
            QTimer::singleShot(0, this, [this, previousScrollMax, previousScrollValue]() {
                if (!m_messageListView) {
                    return;
                }

                QScrollBar *bar = m_messageListView->verticalScrollBar();
                if (!bar) {
                    return;
                }

                const int delta = bar->maximum() - previousScrollMax;
                bar->setValue(previousScrollValue + qMax(0, delta));
            });
        }

        if (conversationId == currentRoomId()) {
            preloadMediaThumbnailsForCurrentConversation();
            preloadMessageAvatarsForCurrentConversation();
        }

        const qint64 pendingFocusMessageId = m_pendingMessageFocusByConversationId.value(conversationId, 0);
        if (pendingFocusMessageId > 0) {
            QTimer::singleShot(0, this, [this, conversationId, pendingFocusMessageId]() {
                m_messageHandler->focusMessageByServerIdInConversation(conversationId, pendingFocusMessageId);
            });
        }

        if (!prepend) {
            const Conversation *updatedConversation = m_chatStore->conversationAt(index);
            const qint64 newestMessageId = updatedConversation && !updatedConversation->messages.isEmpty()
                ? updatedConversation->messages.constLast().serverMessageId
                : 0;
            markConversationReadOnServer(conversationId, newestMessageId);

            setNetworkStatus(UiText::MainWindow::kStatusHistoryLoaded,
                             UiText::MainWindow::kLoadedMessagesPattern
                                 .arg(QString::number(items.size())));
        }
    });
}

void MainWindow::loadConversationMembers(int index)
{
    const Conversation *conversation = m_chatStore->conversationAt(index);
    if (!conversation || !m_conversationMembersLabel) {
        return;
    }

    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        m_conversationMembersLabel->clear();
        return;
    }

    const QString conversationId = conversation->id;

    QUrl url = QUrl::fromUserInput(m_backendBaseUrl);
    url.setPath(QStringLiteral("/api/conversations/%1/members").arg(conversationId));

    const int requestSerial = ++m_membersLoadSerial;
    m_networkService->getJsonAsync(url, [this, requestSerial, index, conversationId](const NetworkService::HttpResult &result) {
        if (requestSerial != m_membersLoadSerial) {
            return;
        }

        const Conversation *latestConversation = m_chatStore->conversationAt(index);
        if (!latestConversation || latestConversation->id != conversationId || !m_conversationMembersLabel) {
            return;
        }

        if (!result.ok) {
            m_currentUserOwnsCurrentGroup = false;
            m_currentGroupOwnerName.clear();
            m_conversationMembersLabel->setText(UiText::MainWindow::kMembersLoadFailed);
            m_conversationMembersLabel->setToolTip(result.message);
            refreshConversationHeader();
            return;
        }

        const QJsonObject conversationObject = result.body.value(QStringLiteral("conversation")).toObject();
        const QString ownerEmail = conversationObject.value(QStringLiteral("ownerEmail")).toString().trimmed();
        const QString ownerNickname = conversationObject.value(QStringLiteral("ownerNickname")).toString().trimmed();
        m_currentUserOwnsCurrentGroup = ownerEmail.compare(m_loggedInUserEmail, Qt::CaseInsensitive) == 0;
        m_currentGroupOwnerName = ownerNickname.isEmpty() ? ownerEmail : ownerNickname;

        const QJsonArray members = result.body.value(QStringLiteral("members")).toArray();
        m_currentConversationMembers.clear();
        m_currentConversationMembers.reserve(members.size());
        QStringList names;
        names.reserve(members.size());
        int onlineCount = 0;
        for (const QJsonValue &value : members) {
            const QJsonObject member = value.toObject();
            const QString nickname = member.value(QStringLiteral("nickname")).toString().trimmed();
            const QString email = member.value(QStringLiteral("email")).toString().trimmed();
            const QString avatarUrl = member.value(QStringLiteral("avatarUrl")).toString().trimmed();
            const bool isOnline = member.value(QStringLiteral("isOnline")).toBool(false);
            const QString lastSeenAtRaw = member.value(QStringLiteral("lastSeenAt")).toString().trimmed();
            const qint64 lastSeenAt = lastSeenAtRaw.isEmpty() ? 0 : ChatUtils::parseBackendTimestamp(lastSeenAtRaw);
            GroupMemberInfo memberInfo;
            memberInfo.email = email;
            memberInfo.nickname = nickname;
            memberInfo.avatarUrl = avatarUrl;
            memberInfo.isOwner = ownerEmail.compare(email, Qt::CaseInsensitive) == 0;
            memberInfo.isSelf = m_loggedInUserEmail.compare(email, Qt::CaseInsensitive) == 0;
            memberInfo.isOnline = isOnline;
            memberInfo.lastSeenAt = lastSeenAt;
            m_currentConversationMembers.append(memberInfo);
            names.append(nickname.isEmpty() ? email : nickname);
            if (isOnline) {
                onlineCount += 1;
            }
        }

        const QString ownerText = m_currentGroupOwnerName.isEmpty() ? UiText::MainWindow::kUnknownOwner
                                                                     : m_currentGroupOwnerName;
        const QString summary = names.isEmpty()
            ? UiText::MainWindow::kOwnerMembersNonePattern.arg(ownerText)
            : UiText::MainWindow::kOwnerMembersPattern.arg(ownerText, names.join(QStringLiteral(", ")));

        const QString onlineSummary =
            UiText::MainWindow::kOnlineSummaryPattern.arg(onlineCount).arg(names.size());
        m_conversationMembersLabel->setText(
            summary + UiText::MainWindow::kMembersTotalPattern.arg(QString::number(names.size()))
            + onlineSummary);
        m_conversationMembersLabel->setToolTip(summary);
        refreshConversationHeader();
        preloadMessageAvatarsForCurrentConversation();
    });
}

void MainWindow::markConversationReadOnServer(const QString &conversationId, qint64 latestServerMessageId)
{
    if (conversationId.trimmed().isEmpty() || latestServerMessageId <= 0) {
        return;
    }
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        return;
    }

    m_conversationManager->markConversationRead(m_backendBaseUrl, conversationId, latestServerMessageId);
}

QString MainWindow::currentRoomId() const
{
    return m_conversationManager->currentRoomId();
}

QString MainWindow::currentRoomName() const
{
    return m_conversationManager->currentRoomName();
}

bool MainWindow::currentConversationIsGroup() const
{
    return m_conversationManager->currentConversationIsGroup();
}

bool MainWindow::selectConversationById(const QString &conversationId)
{
    if (conversationId.trimmed().isEmpty()) {
        return false;
    }

    for (int index = 0; index < m_conversationModel->rowCount(); ++index) {
        const Conversation *conversation = m_chatStore->conversationAt(index);
        if (!conversation || conversation->id != conversationId) {
            continue;
        }

        const QModelIndex modelIndex = m_conversationModel->index(index, 0);
        m_conversationListView->setCurrentIndex(modelIndex);
        m_conversationListView->selectionModel()->setCurrentIndex(
            modelIndex, QItemSelectionModel::ClearAndSelect | QItemSelectionModel::Rows);
        return true;
    }

    return false;
}

void MainWindow::removeMemberByEmail(const QString &memberEmail,
                                     std::function<void(bool ok, const QString &errorMessage)> callback)
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || !currentConversationIsGroup()) {
        if (callback) {
            callback(false, UiText::MainWindow::kNotGroupError);
        }
        return;
    }

    m_conversationManager->removeMember(m_backendBaseUrl, conversation->id, memberEmail);
    if (callback) {
        callback(true, QString());
    }
}

void MainWindow::setNetworkStatus(const QString &text, const QString &detail)
{
    if (!m_networkStatusLabel) {
        return;
    }

    m_networkStatusLabel->setText(networkStatusTextWithQueue(text));
    const QString detailWithQueue = networkStatusDetailWithQueue(detail);
    m_networkStatusLabel->setToolTip(detailWithQueue.isEmpty() ? text : detailWithQueue);

    if (m_showQueueDetailsButton && m_chatStore) {
        const int queuedCount = m_chatStore->queuedMessageCount();
        m_showQueueDetailsButton->setText(
            queuedCount > 0
                ? UiText::MainWindow::kQueueDetailsButtonWithCountPattern.arg(QString::number(queuedCount))
                : UiText::MainWindow::kQueueDetailsButton);
        m_showQueueDetailsButton->setEnabled(queuedCount > 0);
    }
}

QString MainWindow::networkStatusTextWithQueue(const QString &text) const
{
    if (!m_chatStore) {
        return text;
    }

    const int queuedCount = m_chatStore->queuedMessageCount();
    if (queuedCount <= 0) {
        return text;
    }

    return text + UiText::MainWindow::kQueuedMessagesInlinePattern.arg(QString::number(queuedCount));
}

QString MainWindow::networkStatusDetailWithQueue(const QString &detail) const
{
    if (!m_chatStore) {
        return detail;
    }

    const int queuedCount = m_chatStore->queuedMessageCount();
    if (queuedCount <= 0) {
        return detail;
    }

    const QString queuedText =
        UiText::MainWindow::kQueuedMessagesPattern.arg(QString::number(queuedCount));
    if (detail.trimmed().isEmpty()) {
        return queuedText;
    }

    return detail + QStringLiteral("\n") + queuedText;
}

void MainWindow::showProfileDialog()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(UiText::MainWindow::kDialogProfile);
    dialog.resize(400, 300);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);

    auto *avatarLabel = new QLabel(&dialog);
    avatarLabel->setAlignment(Qt::AlignCenter);
    avatarLabel->setFixedSize(88, 88);
    const QString displayName = m_loggedInUserNickname.trimmed().isEmpty()
        ? m_loggedInUserEmail : m_loggedInUserNickname;
    avatarLabel->setText(displayName.isEmpty() ? QStringLiteral("?") : QString(displayName.at(0)).toUpper());
    avatarLabel->setStyleSheet(
        QStringLiteral("background: qlineargradient(x1:0, y1:0, x2:1, y2:1, stop:0 #60A5FA, stop:1 #2563EB);"
                        "color: white; border-radius: 44px; font-size: 28px; font-weight: 900;"));

    if (!m_loggedInUserAvatarUrl.trimmed().isEmpty() && m_networkService && m_networkService->networkManager()) {
        const QUrl avatarUrl = MediaUtils::resolveMediaUrl(m_loggedInUserAvatarUrl.trimmed(), m_backendBaseUrl);
        if (avatarUrl.isValid()) {
            QNetworkRequest request(avatarUrl);
            if (!m_authToken.trimmed().isEmpty() && MediaUtils::isBackendUploadPath(avatarUrl)) {
                request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_authToken).toUtf8());
            }
            QNetworkReply *reply = m_networkService->networkManager()->get(request);
            connect(reply, &QNetworkReply::finished, &dialog, [reply, avatarLabel]() {
                const QByteArray bytes = reply->readAll();
                reply->deleteLater();
                if (!bytes.isEmpty()) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(bytes)) {
                        const int side = qMin(pixmap.width(), pixmap.height());
                        const QRect crop((pixmap.width() - side) / 2, (pixmap.height() - side) / 2, side, side);
                        avatarLabel->setPixmap(pixmap.copy(crop).scaled(88, 88, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation));
                        avatarLabel->setStyleSheet(QStringLiteral("border-radius: 44px; background: transparent;"));
                    }
                }
            });
        }
    }

    layout->addWidget(avatarLabel);

    auto *nicknameEdit = new QLineEdit(m_loggedInUserNickname, &dialog);
    nicknameEdit->setPlaceholderText(QStringLiteral("输入新昵称"));
    layout->addWidget(new QLabel(QStringLiteral("昵称"), &dialog));
    layout->addWidget(nicknameEdit);

    auto *emailLabel = new QLabel(m_loggedInUserEmail, &dialog);
    emailLabel->setStyleSheet(QStringLiteral("color: #6b7280;"));
    layout->addWidget(new QLabel(QStringLiteral("邮箱"), &dialog));
    layout->addWidget(emailLabel);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    buttonBox->addButton(QDialogButtonBox::Save);
    buttonBox->addButton(QDialogButtonBox::Cancel);
    connect(buttonBox, &QDialogButtonBox::accepted, &dialog, [this, nicknameEdit, &dialog]() {
        const QString newNickname = nicknameEdit->text().trimmed();
        if (!newNickname.isEmpty() && newNickname != m_loggedInUserNickname) {
            m_profileManager->updateProfile(m_backendBaseUrl, newNickname, m_loggedInUserAvatarUrl);
        }
        dialog.accept();
    });
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog.exec();
}

void MainWindow::showFriendsDialog()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("好友列表"));
    dialog->resize(500, 420);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(dialog);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *addLayout = new QHBoxLayout();
    auto *emailInput = new QLineEdit(dialog);
    emailInput->setPlaceholderText(QStringLiteral("输入对方邮箱添加好友"));
    auto *addButton = new QPushButton(QStringLiteral("添加"), dialog);
    addLayout->addWidget(emailInput, 1);
    addLayout->addWidget(addButton);
    layout->addLayout(addLayout);

    auto *buttonBox = new QDialogButtonBox(dialog);
    buttonBox->addButton(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(m_profileManager, &ProfileManager::friendsLoaded, dialog, [listWidget](const QJsonObject &result) {
        listWidget->clear();
        const QJsonArray friends = result.value(QStringLiteral("friends")).toArray();
        for (const QJsonValue &value : friends) {
            const QJsonObject f = value.toObject();
            const QString nickname = f.value(QStringLiteral("nickname")).toString().trimmed();
            const QString email = f.value(QStringLiteral("email")).toString().trimmed();
            const QString display = nickname.isEmpty() ? email : QStringLiteral("%1 (%2)").arg(nickname, email);
            auto *item = new QListWidgetItem(display, listWidget);
            item->setData(Qt::UserRole, email);
        }
    });

    connect(addButton, &QPushButton::clicked, dialog, [this, emailInput]() {
        const QString email = emailInput->text().trimmed().toLower();
        if (email.isEmpty()) return;
        m_profileManager->sendFriendRequest(m_backendBaseUrl, email);
        emailInput->clear();
    });

    connect(m_profileManager, &ProfileManager::networkStatusChanged, this, &MainWindow::setNetworkStatus);

    m_profileManager->loadFriends(m_backendBaseUrl);
    dialog->exec();
}

void MainWindow::showFriendRequestsDialog()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("好友请求"));
    dialog->resize(500, 420);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(dialog);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *buttonBox = new QDialogButtonBox(dialog);
    auto *acceptButton = buttonBox->addButton(QStringLiteral("接受"), QDialogButtonBox::AcceptRole);
    auto *rejectButton = buttonBox->addButton(QStringLiteral("拒绝"), QDialogButtonBox::DestructiveRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    acceptButton->setEnabled(false);
    rejectButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(listWidget, &QListWidget::itemSelectionChanged, dialog, [listWidget, acceptButton, rejectButton]() {
        const bool hasSelection = listWidget->currentItem() != nullptr;
        acceptButton->setEnabled(hasSelection);
        rejectButton->setEnabled(hasSelection);
    });

    auto refreshList = [this, listWidget, dialog]() {
        Q_UNUSED(dialog);
        listWidget->clear();
        m_profileManager->loadFriendRequests(m_backendBaseUrl);
    };

    connect(m_profileManager, &ProfileManager::friendRequestsLoaded, dialog, [listWidget](const QJsonObject &result) {
        listWidget->clear();
        const QJsonArray requests = result.value(QStringLiteral("requests")).toArray();
        for (const QJsonValue &value : requests) {
            const QJsonObject r = value.toObject();
            const qint64 requestId = r.value(QStringLiteral("id")).toInteger(0);
            const QString nickname = r.value(QStringLiteral("senderNickname")).toString().trimmed();
            const QString email = r.value(QStringLiteral("senderEmail")).toString().trimmed();
            const QString display = nickname.isEmpty() ? email : QStringLiteral("%1 (%2)").arg(nickname, email);
            auto *item = new QListWidgetItem(display, listWidget);
            item->setData(Qt::UserRole, requestId);
        }
        if (listWidget->count() == 0) {
            listWidget->addItem(QStringLiteral("暂无好友请求"));
        }
    });

    connect(acceptButton, &QPushButton::clicked, dialog, [this, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 requestId = item->data(Qt::UserRole).toLongLong();
        if (requestId <= 0) return;
        m_profileManager->acceptFriendRequest(m_backendBaseUrl, requestId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(rejectButton, &QPushButton::clicked, dialog, [this, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 requestId = item->data(Qt::UserRole).toLongLong();
        if (requestId <= 0) return;
        m_profileManager->rejectFriendRequest(m_backendBaseUrl, requestId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(m_profileManager, &ProfileManager::networkStatusChanged, this, &MainWindow::setNetworkStatus);

    refreshList();
    dialog->exec();
}

void MainWindow::logout()
{
    const auto decision = QMessageBox::question(
        this,
        UiText::MainWindow::kLogoutTitle,
        UiText::MainWindow::kLogoutConfirm);
    if (decision != QMessageBox::Yes) {
        return;
    }

    m_chatClient->disconnectFromServer();
    m_chatStore->clear();

    m_backendBaseUrl.clear();
    m_authToken.clear();
    m_loggedInUserEmail.clear();
    m_loggedInUserNickname.clear();
    m_loggedInUserAvatarUrl.clear();

    m_networkService->setAuthToken(QString());
    m_networkService->setBackendBaseUrl(QString());

    QSettings settings;
    settings.remove(QStringLiteral("auth/user_nickname"));
    settings.remove(QStringLiteral("auth/user_avatar_url"));
    settings.sync();

    ChatUtils::SecureStorage::secureSetValue(QStringLiteral("auth/token"), QString());

    close();
}

void MainWindow::showBlacklistDialog()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("黑名单"));
    dialog->resize(460, 380);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(dialog);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *addLayout = new QHBoxLayout();
    auto *emailInput = new QLineEdit(dialog);
    emailInput->setPlaceholderText(QStringLiteral("输入邮箱拉黑用户"));
    auto *blockButton = new QPushButton(QStringLiteral("拉黑"), dialog);
    addLayout->addWidget(emailInput, 1);
    addLayout->addWidget(blockButton);
    layout->addLayout(addLayout);

    auto *buttonBox = new QDialogButtonBox(dialog);
    auto *unblockButton = buttonBox->addButton(QStringLiteral("解除拉黑"), QDialogButtonBox::ActionRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    unblockButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(listWidget, &QListWidget::itemSelectionChanged, dialog, [listWidget, unblockButton]() {
        unblockButton->setEnabled(listWidget->currentItem() != nullptr);
    });

    connect(m_profileManager, &ProfileManager::blacklistLoaded, dialog, [listWidget](const QJsonObject &result) {
        listWidget->clear();
        const QJsonArray blocked = result.value(QStringLiteral("blockedUsers")).toArray();
        for (const QJsonValue &value : blocked) {
            const QJsonObject u = value.toObject();
            const qint64 userId = u.value(QStringLiteral("id")).toInteger(0);
            const QString nickname = u.value(QStringLiteral("nickname")).toString().trimmed();
            const QString email = u.value(QStringLiteral("email")).toString().trimmed();
            const QString display = nickname.isEmpty() ? email : QStringLiteral("%1 (%2)").arg(nickname, email);
            auto *item = new QListWidgetItem(display, listWidget);
            item->setData(Qt::UserRole, userId);
        }
        if (listWidget->count() == 0) {
            listWidget->addItem(QStringLiteral("黑名单为空"));
        }
    });

    connect(blockButton, &QPushButton::clicked, dialog, [this, emailInput]() {
        const QString email = emailInput->text().trimmed().toLower();
        if (email.isEmpty()) return;
        m_profileManager->blockUser(m_backendBaseUrl, email);
        emailInput->clear();
        m_profileManager->loadBlacklist(m_backendBaseUrl);
    });

    connect(unblockButton, &QPushButton::clicked, dialog, [this, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 userId = item->data(Qt::UserRole).toLongLong();
        if (userId <= 0) return;
        m_profileManager->unblockUser(m_backendBaseUrl, userId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(m_profileManager, &ProfileManager::networkStatusChanged, this, &MainWindow::setNetworkStatus);

    m_profileManager->loadBlacklist(m_backendBaseUrl);
    dialog->exec();
}

void MainWindow::inviteMembersToCurrentConversation()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversation->type != QStringLiteral("group")) {
        setNetworkStatus(QStringLiteral("当前不是群聊"));
        return;
    }

    bool accepted = false;
    const QString emailText = QInputDialog::getText(
        this,
        QStringLiteral("邀请成员"),
        QStringLiteral("输入要邀请的邮箱（多个用逗号分隔）"),
        QLineEdit::Normal,
        QString(),
        &accepted).trimmed();

    if (!accepted || emailText.isEmpty()) {
        return;
    }

    const QStringList emails = emailText.split(QStringLiteral(","), Qt::SkipEmptyParts);
    QJsonArray memberEmails;
    for (const QString &email : emails) {
        const QString trimmed = email.trimmed().toLower();
        if (!trimmed.isEmpty()) {
            memberEmails.append(trimmed);
        }
    }

    if (memberEmails.isEmpty()) {
        return;
    }

    m_conversationManager->inviteMembers(m_backendBaseUrl, conversation->id, memberEmails);
}

void MainWindow::removeMemberFromCurrentConversation()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversation->type != QStringLiteral("group")) {
        setNetworkStatus(QStringLiteral("当前不是群聊"));
        return;
    }

    if (m_currentConversationMembers.isEmpty()) {
        setNetworkStatus(QStringLiteral("暂无成员信息"));
        return;
    }

    QStringList emails;
    QList<QString> emailList;
    for (const GroupMemberInfo &member : std::as_const(m_currentConversationMembers)) {
        if (member.isSelf) continue;
        const QString display = member.nickname.trimmed().isEmpty()
            ? member.email
            : QStringLiteral("%1 (%2)").arg(member.nickname, member.email);
        emails.append(display);
        emailList.append(member.email);
    }

    if (emails.isEmpty()) {
        setNetworkStatus(QStringLiteral("没有可移除的成员"));
        return;
    }

    bool ok = false;
    const QString selected = QInputDialog::getItem(
        this,
        QStringLiteral("移除成员"),
        QStringLiteral("选择要移除的成员"),
        emails,
        0,
        false,
        &ok);

    if (!ok || selected.isEmpty()) {
        return;
    }

    const int index = emails.indexOf(selected);
    if (index < 0 || index >= emailList.size()) {
        return;
    }

    const QString memberEmail = emailList.at(index);
    auto *msgBox = new QMessageBox(this);
    msgBox->setWindowTitle(QStringLiteral("确认移除"));
    msgBox->setText(QStringLiteral("确定要将 %1 移出群聊吗？").arg(memberEmail));
    msgBox->setStandardButtons(QMessageBox::Yes | QMessageBox::No);
    msgBox->setDefaultButton(QMessageBox::No);
    if (msgBox->exec() == QMessageBox::Yes) {
        removeMemberByEmail(memberEmail, nullptr);
        loadConversationMembers(m_chatStore->currentConversationIndex());
    }
}

void MainWindow::showMembersDialog()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation || conversation->type != QStringLiteral("group")) {
        setNetworkStatus(QStringLiteral("当前不是群聊"));
        return;
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QStringLiteral("群成员 - %1").arg(conversation->name.isEmpty() ? conversation->id : conversation->name));
    dialog.resize(420, 380);

    auto *layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(&dialog);
    listWidget->setSelectionMode(QAbstractItemView::NoSelection);

    for (const GroupMemberInfo &member : std::as_const(m_currentConversationMembers)) {
        const QString displayName = member.nickname.trimmed().isEmpty()
            ? member.email : member.nickname;
        QString suffix;
        if (member.isOwner) suffix += QStringLiteral(" [群主]");
        if (member.isSelf) suffix += QStringLiteral(" (我)");
        const QString statusText = member.isOnline ? QStringLiteral(" 在线") : QStringLiteral(" 离线");
        auto *item = new QListWidgetItem(displayName + suffix + statusText, listWidget);
        item->setForeground(member.isOnline ? QColor("#059669") : QColor("#9ca3af"));
    }

    if (listWidget->count() == 0) {
        listWidget->addItem(QStringLiteral("暂无成员信息"));
    }

    layout->addWidget(listWidget, 1);

    auto *buttonBox = new QDialogButtonBox(&dialog);
    buttonBox->addButton(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
    layout->addWidget(buttonBox);

    dialog.exec();
}

void MainWindow::leaveCurrentConversation()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    if (conversation->type != QStringLiteral("group")) {
        setNetworkStatus(QStringLiteral("当前不是群聊"));
        return;
    }

    const auto decision = QMessageBox::question(
        this,
        QStringLiteral("退出群聊"),
        QStringLiteral("确定要退出「%1」吗？").arg(conversation->name.isEmpty() ? conversation->id : conversation->name));

    if (decision != QMessageBox::Yes) {
        return;
    }

    m_conversationManager->leaveConversation(m_backendBaseUrl, conversation->id);
}

void MainWindow::deleteCurrentConversation()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        return;
    }

    const QString name = conversation->name.isEmpty() ? conversation->id : conversation->name;
    const auto decision = QMessageBox::question(
        this,
        QStringLiteral("删除会话"),
        QStringLiteral("确定要删除「%1」吗？").arg(name));

    if (decision != QMessageBox::Yes) {
        return;
    }

    m_conversationManager->deleteCurrentConversation();
    loadConversationData();
}

void MainWindow::togglePinCurrentConversation()
{
    m_conversationManager->togglePinCurrentConversation();
}

void MainWindow::toggleMuteCurrentConversation()
{
    m_conversationManager->toggleMuteCurrentConversation();
}

void MainWindow::onConversationSelected(const QModelIndex &current, const QModelIndex &previous)
{
    Q_UNUSED(previous);

    if (!current.isValid() || !m_chatStore) {
        return;
    }

    const int row = current.row();
    const Conversation *previousConversation = m_chatStore->currentConversation();
    if (previousConversation) {
        saveDraftForConversation(previousConversation->id, m_messageInput ? m_messageInput->text() : QString());
    }

    m_chatStore->setCurrentConversation(row);
    loadMessagesForConversation(row);
    restoreDraftForConversation(currentRoomId());
    m_messageHandler->joinCurrentRoomIfConnected();

    const Conversation *conversation = m_chatStore->currentConversation();
    if (conversation && conversation->type == QStringLiteral("group")) {
        loadConversationMembers(row);
    } else {
        if (m_conversationMembersLabel) m_conversationMembersLabel->clear();
        m_currentConversationMembers.clear();
    }

    refreshConversationHeader();
}

void MainWindow::handleMessageActivated(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const QString content = index.data(MessageListModel::ContentRole).toString();
    QString fileName;
    QString rawUrl;
    if (MediaUtils::parseMediaMessageContent(content, nullptr, &fileName, &rawUrl)) {
        openMediaLinkByIndex(index);
        return;
    }

    editMessage(index);
}

void MainWindow::editMessage(const QModelIndex &index)
{
    if (!index.isValid()) {
        return;
    }

    const qint64 serverMessageId = index.data(MessageListModel::ServerMessageIdRole).toLongLong();
    if (serverMessageId <= 0) {
        return;
    }

    if (!index.data(MessageListModel::IsSelfRole).toBool()) {
        return;
    }

    const QString currentContent = index.data(MessageListModel::ContentRole).toString();
    bool accepted = false;
    const QString newContent = QInputDialog::getText(
        this,
        UiText::MainWindow::kEditMessage,
        UiText::MainWindow::kEditMessage,
        QLineEdit::Normal,
        currentContent,
        &accepted).trimmed();

    if (!accepted || newContent.isEmpty() || newContent == currentContent) {
        return;
    }

    m_messageHandler->editMessage(currentRoomId(), serverMessageId, newContent);
}

void MainWindow::refreshConversationHeader()
{
    const Conversation *conversation = m_chatStore->currentConversation();
    if (!conversation) {
        if (m_conversationTitleLabel) m_conversationTitleLabel->setText(UiText::MainWindow::kLobby);
        if (m_conversationMetaLabel) m_conversationMetaLabel->clear();
        if (m_conversationMembersLabel) m_conversationMembersLabel->clear();
        return;
    }

    if (m_conversationTitleLabel) {
        const QString title = conversation->name.trimmed().isEmpty()
            ? conversation->id : conversation->name;
        m_conversationTitleLabel->setText(title);
    }

    if (m_conversationMetaLabel) {
        const bool isGroup = conversation->type == QStringLiteral("group");
        const QString typeText = isGroup ? QStringLiteral("群聊") : QStringLiteral("私聊");
        QString meta = typeText;
        if (conversation->memberCount > 0) {
            meta += QStringLiteral(" · %1 人").arg(conversation->memberCount);
        }
        if (conversation->onlineCount > 0) {
            meta += QStringLiteral(" · %1 在线").arg(conversation->onlineCount);
        }
        m_conversationMetaLabel->setText(meta);
    }

    const bool isGroup = conversation->type == QStringLiteral("group");
    if (m_showMembersButton) m_showMembersButton->setVisible(isGroup);
    if (m_inviteMembersButton) m_inviteMembersButton->setVisible(isGroup && m_currentUserOwnsCurrentGroup);
    if (m_removeMemberButton) m_removeMemberButton->setVisible(isGroup && m_currentUserOwnsCurrentGroup);
    if (m_leaveConversationButton) m_leaveConversationButton->setVisible(isGroup && !m_currentUserOwnsCurrentGroup);

    updateTypingStatusLabel();
}

void MainWindow::loadCurrentUserProfile()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        return;
    }

    m_profileManager->loadCurrentUserProfile(m_backendBaseUrl);
}

void MainWindow::runGlobalMessageSearch()
{
    if (m_backendBaseUrl.isEmpty() || m_authToken.isEmpty()) {
        setNetworkStatus(UiText::MainWindow::kStatusSignInRequired);
        return;
    }

    bool accepted = false;
    const QString keyword = QInputDialog::getText(
                                this,
                                UiText::MainWindow::kGlobalSearchTitle,
                                UiText::MainWindow::kGlobalSearchPrompt,
                                QLineEdit::Normal,
                                QString(),
                                &accepted)
                                .trimmed();
    if (!accepted || keyword.isEmpty()) {
        return;
    }

    QUrl url = QUrl::fromUserInput(m_backendBaseUrl);
    url.setPath(QStringLiteral("/api/search/messages"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), keyword);
    url.setQuery(query);

    m_networkService->getJsonAsync(url, [this, keyword](const NetworkService::HttpResult &result) {
        if (!result.ok) {
            setNetworkStatus(UiText::MainWindow::kStatusGlobalSearchFailed, result.message);
            return;
        }

        const QJsonArray items = result.body.value(QStringLiteral("results")).toArray();
        if (items.isEmpty()) {
            QMessageBox::information(this, UiText::MainWindow::kGlobalSearchTitle,
                                     UiText::MainWindow::kGlobalSearchNotFound.arg(keyword));
            return;
        }

        QDialog dialog(this);
        dialog.setWindowTitle(UiText::MainWindow::kGlobalSearchResultTitle);
        dialog.resize(680, 460);

        auto *layout = new QVBoxLayout(&dialog);
        layout->setContentsMargins(14, 14, 14, 14);
        layout->setSpacing(10);

        auto *summaryLabel = new QLabel(
            QStringLiteral("\u5173\u952E\u8BCD\u201C%1\u201D\u547D\u4E2D %2 \u6761").arg(keyword).arg(items.size()),
            &dialog);
        layout->addWidget(summaryLabel);

        auto *resultList = new QListWidget(&dialog);
        resultList->setSelectionMode(QAbstractItemView::SingleSelection);
        resultList->setWordWrap(true);

        for (const QJsonValue &value : items) {
            const QJsonObject itemObject = value.toObject();
            const QJsonObject messageObject = itemObject.value(QStringLiteral("message")).toObject();
            const QString conversationId = itemObject.value(QStringLiteral("conversationId")).toString();
            const QString conversationName = itemObject.value(QStringLiteral("conversationName")).toString();
            const qint64 messageId = messageObject.value(QStringLiteral("id")).toInteger(0);
            const QString senderNickname = messageObject.value(QStringLiteral("senderNickname")).toString();
            const QString content = messageObject.value(QStringLiteral("content")).toString();
            const QString createdAt = messageObject.value(QStringLiteral("createdAt")).toString();

            QString preview = QStringLiteral("[%1] %2: %3")
                                  .arg(conversationName.isEmpty() ? conversationId : conversationName,
                                       senderNickname,
                                       content);
            if (!createdAt.trimmed().isEmpty()) {
                preview += QStringLiteral("\n") + createdAt;
            }

            auto *listItem = new QListWidgetItem(preview, resultList);
            listItem->setData(Qt::UserRole, conversationId);
            listItem->setData(Qt::UserRole + 1, messageId);
        }

        layout->addWidget(resultList, 1);

        auto *buttonBox = new QDialogButtonBox(&dialog);
        QPushButton *locateButton = buttonBox->addButton(QStringLiteral("定位消息"), QDialogButtonBox::ActionRole);
        buttonBox->addButton(QDialogButtonBox::Close);
        locateButton->setEnabled(false);

        connect(resultList, &QListWidget::itemSelectionChanged, &dialog, [resultList, locateButton]() {
            locateButton->setEnabled(resultList->currentItem() != nullptr);
        });

        const auto locateSelected = [this, resultList, &dialog]() {
            QListWidgetItem *selectedItem = resultList->currentItem();
            if (!selectedItem) {
                return;
            }

            const QString conversationId = selectedItem->data(Qt::UserRole).toString().trimmed();
            const qint64 messageId = selectedItem->data(Qt::UserRole + 1).toLongLong();
            if (conversationId.isEmpty() || messageId <= 0) {
                return;
            }

            if (!selectConversationById(conversationId)) {
                loadConversationData();
                setNetworkStatus(UiText::MainWindow::kStatusLocateMessageFailed, UiText::MainWindow::kDetailTargetConversationMissing);
                return;
            }

            const int conversationIndex = m_chatStore->currentConversationIndex();
            if (conversationIndex < 0) {
                return;
            }

            m_pendingMessageFocusByConversationId.insert(conversationId, messageId);
            loadMessagesForConversation(conversationIndex, messageId + 1, false);
            dialog.accept();
        };

        connect(locateButton, &QPushButton::clicked, &dialog, locateSelected);
        connect(resultList, &QListWidget::itemDoubleClicked, &dialog, [locateSelected](QListWidgetItem *) {
            locateSelected();
        });
        connect(buttonBox, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

        layout->addWidget(buttonBox);
        dialog.exec();
    });
}

void MainWindow::updateProfileAvatarBadge()
{
    if (!m_profileAvatarLabel) {
        return;
    }

    const QString displayName = m_loggedInUserNickname.trimmed().isEmpty()
        ? m_loggedInUserEmail.trimmed() : m_loggedInUserNickname.trimmed();
    const QString initial = displayName.isEmpty() ? QStringLiteral("?")
        : QString(displayName.at(0)).toUpper();

    if (!m_loggedInUserAvatarUrl.trimmed().isEmpty() && m_networkService && m_networkService->networkManager()) {
        const QUrl avatarUrl = MediaUtils::resolveMediaUrl(m_loggedInUserAvatarUrl.trimmed(), m_backendBaseUrl);
        if (avatarUrl.isValid()) {
            QNetworkRequest request(avatarUrl);
            if (!m_authToken.trimmed().isEmpty() && MediaUtils::isBackendUploadPath(avatarUrl)) {
                request.setRawHeader("Authorization", QStringLiteral("Bearer %1").arg(m_authToken).toUtf8());
            }
            QNetworkReply *reply = m_networkService->networkManager()->get(request);
            connect(reply, &QNetworkReply::finished, this, [this, reply, initial]() {
                const QByteArray bytes = reply->readAll();
                reply->deleteLater();
                if (!bytes.isEmpty()) {
                    QPixmap pixmap;
                    if (pixmap.loadFromData(bytes)) {
                        const QPixmap scaled = pixmap.scaled(42, 42, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
                        const int side = qMin(scaled.width(), scaled.height());
                        const QRect crop((scaled.width() - side) / 2, (scaled.height() - side) / 2, side, side);
                        const QPixmap cropped = scaled.copy(crop);
                        m_profileAvatarLabel->setPixmap(cropped);
                        m_profileAvatarLabel->setStyleSheet(
                            QStringLiteral("border-radius: 21px; background: transparent;"));
                        if (m_messageDelegate) {
                            m_messageDelegate->setSelfAvatarPixmap(cropped);
                            if (m_messageListView && m_messageListView->viewport()) {
                                m_messageListView->viewport()->update();
                            }
                        }
                        return;
                    }
                }
                m_profileAvatarLabel->setText(initial);
            });
            return;
        }
    }

    m_profileAvatarLabel->setText(initial);
}

void MainWindow::refreshMessageSearchMatches(bool scrollToCurrent)
{
    Q_UNUSED(scrollToCurrent);
    updateMessageSearchUi();
}

void MainWindow::jumpMessageSearchMatch(int step)
{
    Q_UNUSED(step);
}

void MainWindow::updateMessageSearchUi()
{
    if (!m_messageSearchResultLabel) {
        return;
    }

    m_messageSearchResultLabel->setText(QStringLiteral("0/0"));
    if (m_messageSearchPrevButton) m_messageSearchPrevButton->setEnabled(false);
    if (m_messageSearchNextButton) m_messageSearchNextButton->setEnabled(false);
}

void MainWindow::startReply(qint64 messageId, const QString &content, const QString &sender)
{
    m_replyToMessageId = messageId;
    m_replyToContent = content;
    m_replyToSender = sender;

    const QString preview = QStringLiteral("回复 %1: %2").arg(sender, content.left(80));
    m_replyPreviewLabel->setText(preview);
    m_replyBar->setVisible(true);
    m_messageInput->setFocus();
}

void MainWindow::cancelReply()
{
    m_replyToMessageId = 0;
    m_replyToContent.clear();
    m_replyToSender.clear();
    m_replyBar->setVisible(false);
}



