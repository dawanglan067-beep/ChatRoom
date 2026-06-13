#include "timeformatutils.h"

#include <QDate>
#include <QDateTime>

namespace
{
QString weekdayLabel(int dayOfWeek)
{
    switch (dayOfWeek) {
    case 1:
        return QStringLiteral("\u5468\u4e00");
    case 2:
        return QStringLiteral("\u5468\u4e8c");
    case 3:
        return QStringLiteral("\u5468\u4e09");
    case 4:
        return QStringLiteral("\u5468\u56db");
    case 5:
        return QStringLiteral("\u5468\u4e94");
    case 6:
        return QStringLiteral("\u5468\u516d");
    case 7:
    default:
        return QStringLiteral("\u5468\u65e5");
    }
}
}

QString formatConversationTimeLabel(qint64 timestampMs)
{
    if (timestampMs <= 0) {
        return QString();
    }

    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    if (!dateTime.isValid()) {
        return QString();
    }

    const QDate date = dateTime.date();
    const QDate today = QDate::currentDate();
    if (date == today) {
        return dateTime.toString(QStringLiteral("HH:mm"));
    }
    if (date == today.addDays(-1)) {
        return QStringLiteral("\u6628\u5929 %1").arg(dateTime.toString(QStringLiteral("HH:mm")));
    }

    const int daysAgo = date.daysTo(today);
    if (daysAgo >= 2 && daysAgo <= 6) {
        return weekdayLabel(date.dayOfWeek());
    }
    if (date.year() == today.year()) {
        return dateTime.toString(QStringLiteral("MM-dd"));
    }

    return dateTime.toString(QStringLiteral("yyyy-MM-dd"));
}

QString formatMessageTimeLabel(qint64 timestampMs)
{
    if (timestampMs <= 0) {
        return QString();
    }

    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    if (!dateTime.isValid()) {
        return QString();
    }

    const QDate date = dateTime.date();
    const QDate today = QDate::currentDate();
    if (date == today) {
        return QStringLiteral("\u4eca\u5929 %1").arg(dateTime.toString(QStringLiteral("HH:mm")));
    }
    if (date == today.addDays(-1)) {
        return QStringLiteral("\u6628\u5929 %1").arg(dateTime.toString(QStringLiteral("HH:mm")));
    }

    const int daysAgo = date.daysTo(today);
    if (daysAgo >= 2 && daysAgo <= 6) {
        return QStringLiteral("%1 %2")
            .arg(weekdayLabel(date.dayOfWeek()), dateTime.toString(QStringLiteral("HH:mm")));
    }
    if (date.year() == today.year()) {
        return dateTime.toString(QStringLiteral("MM-dd HH:mm"));
    }

    return dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm"));
}

QString formatAbsoluteDateTimeLabel(qint64 timestampMs)
{
    if (timestampMs <= 0) {
        return QString();
    }

    const QDateTime dateTime = QDateTime::fromMSecsSinceEpoch(timestampMs);
    if (!dateTime.isValid()) {
        return QString();
    }

    return dateTime.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}
