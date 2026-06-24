#include "authdialog.h"

#include "authservice.h"
#include "uitexts.h"

#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>
#include <QRegularExpression>

namespace
{
QString normalizeEmail(const QString &input)
{
    const QString trimmed = input.trimmed();
    if (trimmed.isEmpty()) {
        return QString();
    }

    static const QRegularExpression qqNumberRegex(QStringLiteral("^\\d{5,12}$"));
    if (qqNumberRegex.match(trimmed).hasMatch()) {
        return trimmed + QStringLiteral("@qq.com");
    }

    if (trimmed.contains(QLatin1Char('@'))) {
        return trimmed.toLower();
    }

    return QString();
}
}

AuthDialog::AuthDialog(AuthService *authService, QWidget *parent)
    : QDialog(parent)
    , m_authService(authService)
{
    setupUi();
    loadSettings();
}

QString AuthDialog::authenticatedEmail() const
{
    return m_authenticatedEmail;
}

void AuthDialog::saveBackendSettings()
{
    QString errorMessage;
    if (!m_authService->saveBackendBaseUrl(m_backendUrlInput->text().trimmed(), &errorMessage)) {
        showStatusMessage(errorMessage, false);
        return;
    }

    const QString email = normalizeEmail(m_emailInput->text().trimmed());
    QSettings settings;
    settings.setValue(QStringLiteral("auth/last_email"), email);
    settings.sync();

    showStatusMessage(UiText::AuthDialog::kBackendUrlSaved, true);
}

void AuthDialog::requestCode()
{
    QString errorMessage;
    if (!m_authService->saveBackendBaseUrl(m_backendUrlInput->text().trimmed(), &errorMessage)) {
        showStatusMessage(errorMessage, false);
        return;
    }

    const QString email = normalizeEmail(m_emailInput->text().trimmed());
    if (email.isEmpty()) {
        showStatusMessage(UiText::AuthDialog::kEmailPlaceholder, false);
        return;
    }

    QSettings settings;
    settings.setValue(QStringLiteral("auth/last_email"), email);
    settings.sync();

    setBusy(true);
    showStatusMessage(UiText::AuthDialog::kRequestingCode, true);

    m_authService->sendVerificationCodeAsync(m_backendUrlInput->text().trimmed(),
                                             email,
                                             [this](const SendCodeResult &result) {
                                                 setBusy(false);
                                                 showStatusMessage(result.message, result.ok);
                                             });
}

void AuthDialog::verifyAndEnter()
{
    QString errorMessage;
    if (!m_authService->saveBackendBaseUrl(m_backendUrlInput->text().trimmed(), &errorMessage)) {
        showStatusMessage(errorMessage, false);
        return;
    }

    const QString email = normalizeEmail(m_emailInput->text().trimmed());
    if (email.isEmpty()) {
        showStatusMessage(UiText::AuthDialog::kEmailPlaceholder, false);
        return;
    }

    setBusy(true);
    showStatusMessage(UiText::AuthDialog::kVerifyingCode, true);

    m_authService->verifyCodeAndAuthenticateAsync(
        m_backendUrlInput->text().trimmed(),
        email,
        m_codeInput->text().trimmed(),
        [this](const VerifyAuthResult &result) {
            setBusy(false);
            showStatusMessage(result.message, result.ok);
            if (!result.ok) {
                return;
            }

            m_authenticatedEmail = m_authService->currentUserEmail();
            accept();
        });
}

void AuthDialog::setupUi()
{
    setWindowTitle(UiText::AuthDialog::kWindowTitle);
    setModal(true);
    resize(620, 340);

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(22, 22, 22, 22);
    rootLayout->setSpacing(14);

    auto *titleLabel = new QLabel(UiText::AuthDialog::kTitle, this);
    titleLabel->setStyleSheet(QStringLiteral("font-size: 24px; font-weight: 700; color: #111827;"));

    auto *descLabel = new QLabel(UiText::AuthDialog::kDescription, this);
    descLabel->setWordWrap(true);
    descLabel->setStyleSheet(QStringLiteral("color: #4b5563; font-size: 13px;"));

    auto *formLayout = new QFormLayout();
    formLayout->setLabelAlignment(Qt::AlignRight);
    formLayout->setHorizontalSpacing(12);
    formLayout->setVerticalSpacing(12);

    m_backendUrlInput = new QLineEdit(this);
    m_backendUrlInput->setPlaceholderText(UiText::AuthDialog::kBackendUrlPlaceholder);

    m_emailInput = new QLineEdit(this);
    m_emailInput->setPlaceholderText(UiText::AuthDialog::kEmailPlaceholder);

    m_codeInput = new QLineEdit(this);
    m_codeInput->setPlaceholderText(UiText::AuthDialog::kCodePlaceholder);
    m_codeInput->setMaxLength(6);

    formLayout->addRow(UiText::AuthDialog::kBackendUrlLabel, m_backendUrlInput);
    formLayout->addRow(UiText::AuthDialog::kEmailLabel, m_emailInput);
    formLayout->addRow(UiText::AuthDialog::kCodeLabel, m_codeInput);

    auto *buttonRow = new QHBoxLayout();
    buttonRow->setSpacing(10);

    m_saveConfigButton = new QPushButton(UiText::AuthDialog::kSaveButton, this);
    m_sendCodeButton = new QPushButton(UiText::AuthDialog::kSendCodeButton, this);
    m_enterButton = new QPushButton(UiText::AuthDialog::kVerifyEnterButton, this);

    buttonRow->addWidget(m_saveConfigButton);
    buttonRow->addWidget(m_sendCodeButton);
    buttonRow->addStretch(1);
    buttonRow->addWidget(m_enterButton);

    m_statusLabel = new QLabel(UiText::AuthDialog::kInitialStatus, this);
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setStyleSheet(QStringLiteral("color: #6b7280; font-size: 13px;"));

    rootLayout->addWidget(titleLabel);
    rootLayout->addWidget(descLabel);
    rootLayout->addLayout(formLayout);
    rootLayout->addLayout(buttonRow);
    rootLayout->addWidget(m_statusLabel);

    setStyleSheet(QStringLiteral(
        "QDialog { background: #f8fafc; }"
        "QLineEdit { min-height: 40px; border: 1px solid #d1d5db; border-radius: 14px; padding: 0 12px; background: white; color: #111827; placeholder-text-color: #6b7280; selection-background-color: #bfdbfe; selection-color: #0f172a; }"
        "QLineEdit:focus { border: 1px solid #60a5fa; }"
        "QPushButton { min-height: 40px; border: none; border-radius: 14px; padding: 0 18px; background: #2563eb; color: white; font-weight: 700; }"
        "QPushButton:hover { background: #1d4ed8; }"
        "QLabel { color: #111827; }"));

    connect(m_saveConfigButton, &QPushButton::clicked, this, &AuthDialog::saveBackendSettings);
    connect(m_sendCodeButton, &QPushButton::clicked, this, &AuthDialog::requestCode);
    connect(m_enterButton, &QPushButton::clicked, this, &AuthDialog::verifyAndEnter);
}

void AuthDialog::loadSettings()
{
    QSettings settings;
    m_backendUrlInput->setText(
        settings.value(QStringLiteral("auth/backend_base_url"),
                       QStringLiteral("http://127.0.0.1:3000"))
            .toString());
    m_emailInput->setText(settings.value(QStringLiteral("auth/last_email")).toString());
}

void AuthDialog::showStatusMessage(const QString &message, bool success)
{
    m_statusLabel->setText(message);
    m_statusLabel->setStyleSheet(QStringLiteral("color: %1; font-size: 13px;")
                                     .arg(success ? QStringLiteral("#0f9d58") : QStringLiteral("#dc2626")));
}

void AuthDialog::setBusy(bool busy)
{
    m_busy = busy;

    if (m_backendUrlInput) {
        m_backendUrlInput->setEnabled(!busy);
    }
    if (m_emailInput) {
        m_emailInput->setEnabled(!busy);
    }
    if (m_codeInput) {
        m_codeInput->setEnabled(!busy);
    }
    if (m_saveConfigButton) {
        m_saveConfigButton->setEnabled(!busy);
    }
    if (m_sendCodeButton) {
        m_sendCodeButton->setEnabled(!busy);
    }
    if (m_enterButton) {
        m_enterButton->setEnabled(!busy);
    }
}
