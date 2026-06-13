#pragma once

#include <QDialog>

class AuthService;
class QLabel;
class QLineEdit;
class QPushButton;

class AuthDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AuthDialog(AuthService *authService, QWidget *parent = nullptr);
    QString authenticatedEmail() const;

private slots:
    void saveBackendSettings();
    void requestCode();
    void verifyAndEnter();

private:
    void setupUi();
    void loadSettings();
    void showStatusMessage(const QString &message, bool success);
    void setBusy(bool busy);

    AuthService *m_authService = nullptr;
    QString m_authenticatedEmail;

    QLineEdit *m_backendUrlInput = nullptr;
    QLineEdit *m_emailInput = nullptr;
    QLineEdit *m_codeInput = nullptr;
    QLabel *m_statusLabel = nullptr;
    QPushButton *m_saveConfigButton = nullptr;
    QPushButton *m_sendCodeButton = nullptr;
    QPushButton *m_enterButton = nullptr;
    bool m_busy = false;
};
