#include "socialdialogs.h"
#include "profilemanager.h"
#include "uitexts.h"

#include <QAbstractItemView>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QVBoxLayout>

FriendsDialog::FriendsDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(UiText::MainWindow::kFriendsList);
    resize(500, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(this);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *addLayout = new QHBoxLayout();
    auto *emailInput = new QLineEdit(this);
    emailInput->setPlaceholderText(UiText::MainWindow::kAddFriendPlaceholder);
    auto *addButton = new QPushButton(UiText::MainWindow::kAddButton, this);
    addLayout->addWidget(emailInput, 1);
    addLayout->addWidget(addButton);
    layout->addLayout(addLayout);

    auto *buttonBox = new QDialogButtonBox(this);
    buttonBox->addButton(QDialogButtonBox::Close);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(profileManager, &ProfileManager::friendsLoaded, this, [listWidget](const QJsonObject &result) {
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

    connect(addButton, &QPushButton::clicked, this, [this, profileManager, backendBaseUrl, emailInput]() {
        const QString email = emailInput->text().trimmed().toLower();
        if (email.isEmpty()) return;
        profileManager->sendFriendRequest(backendBaseUrl, email);
        emailInput->clear();
    });

    connect(profileManager, &ProfileManager::networkStatusChanged, this, [this](const QString &s, const QString &d) {
        Q_UNUSED(s);
        Q_UNUSED(d);
    });

    profileManager->loadFriends(backendBaseUrl);
    exec();
}

FriendRequestsDialog::FriendRequestsDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(UiText::MainWindow::kFriendRequests);
    resize(500, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(this);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *acceptButton = buttonBox->addButton(UiText::MainWindow::kAccept, QDialogButtonBox::AcceptRole);
    auto *rejectButton = buttonBox->addButton(UiText::MainWindow::kReject, QDialogButtonBox::DestructiveRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    acceptButton->setEnabled(false);
    rejectButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(listWidget, &QListWidget::itemSelectionChanged, this, [listWidget, acceptButton, rejectButton]() {
        const bool hasSelection = listWidget->currentItem() != nullptr;
        acceptButton->setEnabled(hasSelection);
        rejectButton->setEnabled(hasSelection);
    });

    connect(profileManager, &ProfileManager::friendRequestsLoaded, this, [listWidget](const QJsonObject &result) {
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
            listWidget->addItem(UiText::MainWindow::kNoFriendRequests);
        }
    });

    connect(acceptButton, &QPushButton::clicked, this, [profileManager, backendBaseUrl, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 requestId = item->data(Qt::UserRole).toLongLong();
        if (requestId <= 0) return;
        profileManager->acceptFriendRequest(backendBaseUrl, requestId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(rejectButton, &QPushButton::clicked, this, [profileManager, backendBaseUrl, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 requestId = item->data(Qt::UserRole).toLongLong();
        if (requestId <= 0) return;
        profileManager->rejectFriendRequest(backendBaseUrl, requestId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(profileManager, &ProfileManager::networkStatusChanged, this, [this](const QString &s, const QString &d) {
        Q_UNUSED(s);
        Q_UNUSED(d);
    });

    profileManager->loadFriendRequests(backendBaseUrl);
    exec();
}

BlacklistDialog::BlacklistDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(UiText::MainWindow::kBlacklist);
    resize(460, 380);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(10);

    auto *listWidget = new QListWidget(this);
    listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(listWidget, 1);

    auto *addLayout = new QHBoxLayout();
    auto *emailInput = new QLineEdit(this);
    emailInput->setPlaceholderText(UiText::MainWindow::kBlockEmailPlaceholder);
    auto *blockButton = new QPushButton(UiText::MainWindow::kBlockButton, this);
    addLayout->addWidget(emailInput, 1);
    addLayout->addWidget(blockButton);
    layout->addLayout(addLayout);

    auto *buttonBox = new QDialogButtonBox(this);
    auto *unblockButton = buttonBox->addButton(UiText::MainWindow::kUnblockUser, QDialogButtonBox::ActionRole);
    buttonBox->addButton(QDialogButtonBox::Close);
    unblockButton->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttonBox);

    connect(listWidget, &QListWidget::itemSelectionChanged, this, [listWidget, unblockButton]() {
        unblockButton->setEnabled(listWidget->currentItem() != nullptr);
    });

    connect(profileManager, &ProfileManager::blacklistLoaded, this, [listWidget](const QJsonObject &result) {
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
            listWidget->addItem(UiText::MainWindow::kBlacklistEmptyItem);
        }
    });

    connect(blockButton, &QPushButton::clicked, this, [profileManager, backendBaseUrl, emailInput]() {
        const QString email = emailInput->text().trimmed().toLower();
        if (email.isEmpty()) return;
        profileManager->blockUser(backendBaseUrl, email);
        emailInput->clear();
        profileManager->loadBlacklist(backendBaseUrl);
    });

    connect(unblockButton, &QPushButton::clicked, this, [profileManager, backendBaseUrl, listWidget]() {
        QListWidgetItem *item = listWidget->currentItem();
        if (!item) return;
        const qint64 userId = item->data(Qt::UserRole).toLongLong();
        if (userId <= 0) return;
        profileManager->unblockUser(backendBaseUrl, userId);
        delete listWidget->takeItem(listWidget->row(item));
    });

    connect(profileManager, &ProfileManager::networkStatusChanged, this, [this](const QString &s, const QString &d) {
        Q_UNUSED(s);
        Q_UNUSED(d);
    });

    profileManager->loadBlacklist(backendBaseUrl);
    exec();
}
