#include "smtpmailer.h"
#include "timeformatutils.h"

#include <QByteArray>
#include <QDateTime>
#include <QSettings>
#include <QSslSocket>

namespace
{
struct MailSettings
{
    QString senderEmail;
    QString authCode;
    QString smtpHost = QStringLiteral("smtp.qq.com");
    quint16 port = 465;
};

MailSettings loadMailSettings()
{
    QSettings settings;
    MailSettings mailSettings;
    mailSettings.senderEmail = settings.value(QStringLiteral("mail/qq_email")).toString().trimmed();
    mailSettings.authCode = settings.value(QStringLiteral("mail/qq_auth_code")).toString().trimmed();
    return mailSettings;
}

QString explainSmtpFailure(const QString &response)
{
    const QString trimmed = response.trimmed();
    if (trimmed.contains(QStringLiteral("535"))) {
        return QStringLiteral(
            "QQ 邮箱拒绝了 SMTP 登录。\n"
            "请检查：\n"
            "1. 是否已经在 QQ 邮箱设置中开启 POP3/SMTP 服务；\n"
            "2. 当前填写的是 SMTP 授权码，而不是 QQ 登录密码；\n"
            "3. 授权码是否刚刚重置过，旧授权码已经失效；\n"
            "4. QQ 邮箱是否触发了安全限制或发送频率限制。\n"
            "服务器原始返回：%1").arg(trimmed);
    }

    return QStringLiteral("SMTP 返回异常：%1").arg(trimmed);
}

QString readResponse(QSslSocket &socket, int timeoutMs, QString *errorMessage)
{
    if (!socket.waitForReadyRead(timeoutMs)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("等待 SMTP 响应超时：%1").arg(socket.errorString());
        }
        return {};
    }

    QByteArray response = socket.readAll();
    while (socket.waitForReadyRead(150)) {
        response += socket.readAll();
    }

    return QString::fromUtf8(response);
}

bool responseMatches(const QString &response, const QString &expectedCode)
{
    const QStringList lines = response.split(QStringLiteral("\r\n"), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.startsWith(expectedCode)) {
            return true;
        }
    }
    return false;
}

bool sendCommand(QSslSocket &socket, const QByteArray &command, const QString &expectedCode,
                 QString *errorMessage)
{
    socket.write(command);
    if (!socket.waitForBytesWritten(5000)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("发送 SMTP 命令失败：%1").arg(socket.errorString());
        }
        return false;
    }

    const QString response = readResponse(socket, 5000, errorMessage);
    if (response.isEmpty()) {
        return false;
    }

    if (!responseMatches(response, expectedCode)) {
        if (errorMessage) {
            *errorMessage = explainSmtpFailure(response);
        }
        return false;
    }

    return true;
}

QString buildMailPayload(const QString &senderEmail, const QString &recipientEmail, const QString &code)
{
    const QString subject = QStringLiteral("ChatRoom 验证码");
    const QString body =
        QStringLiteral("您好：\r\n\r\n"
                       "您的 ChatRoom 注册/登录验证码为：%1\r\n"
                       "验证码 5 分钟内有效，请勿泄露给他人。\r\n\r\n"
                       "发送时间：%2\r\n")
            .arg(code, formatAbsoluteDateTimeLabel(QDateTime::currentMSecsSinceEpoch()));

    return QStringLiteral("From: ChatRoom <%1>\r\n"
                          "To: <%2>\r\n"
                          "Subject: =?UTF-8?B?%3?=\r\n"
                          "MIME-Version: 1.0\r\n"
                          "Content-Type: text/plain; charset=\"UTF-8\"\r\n"
                          "Content-Transfer-Encoding: 8bit\r\n"
                          "\r\n"
                          "%4\r\n.\r\n")
        .arg(senderEmail,
             recipientEmail,
             QString::fromUtf8(subject.toUtf8().toBase64()),
             body);
}
}

MailSendResult SmtpMailer::sendVerificationEmail(const QString &recipientEmail, const QString &code)
{
    const MailSettings settings = loadMailSettings();
    if (settings.senderEmail.isEmpty() || settings.authCode.isEmpty()) {
        return {
            false,
            QStringLiteral("请先在登录窗口填写 QQ 发件邮箱和 SMTP 授权码，并点击“保存邮箱配置”。")
        };
    }

    QSslSocket socket;
    socket.connectToHostEncrypted(settings.smtpHost, settings.port);
    if (!socket.waitForEncrypted(8000)) {
        return { false, QStringLiteral("无法连接 QQ 邮箱 SMTP：%1").arg(socket.errorString()) };
    }

    QString errorMessage;
    const QString greeting = readResponse(socket, 5000, &errorMessage);
    if (greeting.isEmpty()) {
        return { false, errorMessage };
    }
    if (!responseMatches(greeting, QStringLiteral("220"))) {
        return { false, QStringLiteral("SMTP 握手失败：%1").arg(greeting.trimmed()) };
    }

    if (!sendCommand(socket, QByteArrayLiteral("EHLO chatroom.local\r\n"), QStringLiteral("250"), &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket, QByteArrayLiteral("AUTH LOGIN\r\n"), QStringLiteral("334"), &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket,
                     settings.senderEmail.toUtf8().toBase64() + QByteArrayLiteral("\r\n"),
                     QStringLiteral("334"),
                     &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket,
                     settings.authCode.toUtf8().toBase64() + QByteArrayLiteral("\r\n"),
                     QStringLiteral("235"),
                     &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket,
                     QStringLiteral("MAIL FROM:<%1>\r\n").arg(settings.senderEmail).toUtf8(),
                     QStringLiteral("250"),
                     &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket,
                     QStringLiteral("RCPT TO:<%1>\r\n").arg(recipientEmail).toUtf8(),
                     QStringLiteral("250"),
                     &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket, QByteArrayLiteral("DATA\r\n"), QStringLiteral("354"), &errorMessage)) {
        return { false, errorMessage };
    }

    if (!sendCommand(socket,
                     buildMailPayload(settings.senderEmail, recipientEmail, code).toUtf8(),
                     QStringLiteral("250"),
                     &errorMessage)) {
        return { false, errorMessage };
    }

    socket.write(QByteArrayLiteral("QUIT\r\n"));
    socket.waitForBytesWritten(1000);
    socket.disconnectFromHost();

    return { true, QStringLiteral("验证码已发送，请到邮箱查收。") };

}
