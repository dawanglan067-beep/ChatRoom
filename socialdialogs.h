#pragma once

#include <QDialog>

class ProfileManager;

class FriendsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FriendsDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent = nullptr);
};

class FriendRequestsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit FriendRequestsDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent = nullptr);
};

class BlacklistDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BlacklistDialog(ProfileManager *profileManager, const QString &backendBaseUrl, QWidget *parent = nullptr);
};
