#pragma once

#include <QString>

struct MailSendResult
{
    bool ok = false;
    QString message;
};

class SmtpMailer
{
public:
};
