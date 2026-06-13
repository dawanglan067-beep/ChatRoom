#include "mediautils.h"

#include <QRegularExpression>
#include <QStringList>
#include <QUrl>

namespace MediaUtils
{

bool parseMediaMessageContent(const QString &content, bool *isImageOut, QString *fileNameOut, QString *urlOut,
                              qint64 *sizeOut, QString *mimeTypeOut)
{
    const QString normalized = content.trimmed();
    if (normalized.isEmpty()) {
        return false;
    }
    const QStringList lines = normalized.split(QRegularExpression(QStringLiteral("[\r\n]+")),
                                               Qt::SkipEmptyParts);
    if (lines.size() < 2) {
        return false;
    }

    const QString firstLine = lines.at(0).trimmed();
    static const QRegularExpression kMediaPrefixRegex(
        QStringLiteral("^\\[([^\\]]+)\\]\\s*(.*)$"),
        QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch prefixMatch = kMediaPrefixRegex.match(firstLine);
    if (!prefixMatch.hasMatch()) {
        return false;
    }

    const QString mediaTag = prefixMatch.captured(1).trimmed().toLower();
    const bool isImage = mediaTag == QStringLiteral("图片")
        || mediaTag == QStringLiteral("image")
        || mediaTag == QStringLiteral("img")
        || mediaTag == QStringLiteral("photo");
    const bool isFile = mediaTag == QStringLiteral("文件")
        || mediaTag == QStringLiteral("file")
        || mediaTag == QStringLiteral("attachment");
    if (!isImage && !isFile) {
        return false;
    }

    QString urlLine = lines.at(1).trimmed();
    static const QRegularExpression kUrlPrefixRegex(
        QStringLiteral("^(?:url|link|链接)\\s*[:：]\\s*"),
        QRegularExpression::CaseInsensitiveOption);
    urlLine.remove(kUrlPrefixRegex);
    if (urlLine.isEmpty()) {
        return false;
    }

    if (isImageOut) {
        *isImageOut = isImage;
    }
    if (fileNameOut) {
        const QString fileName = prefixMatch.captured(2).trimmed();
        *fileNameOut = fileName.isEmpty() ? firstLine : fileName;
    }
    if (urlOut) {
        *urlOut = urlLine;
    }
    if (sizeOut) {
        *sizeOut = -1;
    }
    if (mimeTypeOut) {
        mimeTypeOut->clear();
    }
    static const QRegularExpression kSizeRegex(
        QStringLiteral("^(?:size|大小)\\s*[:：]\\s*(\\d+)"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression kMimeRegex(
        QStringLiteral("^(?:mime|type|类型)\\s*[:：]\\s*(.+)$"),
        QRegularExpression::CaseInsensitiveOption);
    for (int i = 2; i < lines.size(); ++i) {
        const QString line = lines.at(i).trimmed();
        const QRegularExpressionMatch sizeMatch = kSizeRegex.match(line);
        if (sizeMatch.hasMatch() && sizeOut) {
            *sizeOut = sizeMatch.captured(1).toLongLong();
            continue;
        }
        const QRegularExpressionMatch mimeMatch = kMimeRegex.match(line);
        if (mimeMatch.hasMatch() && mimeTypeOut) {
            *mimeTypeOut = mimeMatch.captured(1).trimmed();
        }
    }
    return true;
}

QUrl resolveMediaUrl(const QString &rawUrl, const QString &backendBaseUrl)
{
    const QString trimmedUrl = rawUrl.trimmed();
    if (trimmedUrl.startsWith(QLatin1Char('/'))) {
        QUrl base = QUrl::fromUserInput(backendBaseUrl.trimmed());
        if (!base.isValid() || base.scheme().isEmpty()) {
            return QUrl();
        }
        base.setPath(trimmedUrl);
        base.setQuery(QString());
        base.setFragment(QString());
        return base;
    }

    QUrl base = QUrl::fromUserInput(backendBaseUrl.trimmed());
    QUrl url = QUrl::fromUserInput(trimmedUrl);
    if (!url.isRelative()) {
        return url;
    }

    if (!base.isValid() || base.scheme().isEmpty()) {
        return QUrl();
    }
    return base.resolved(QUrl(trimmedUrl));
}

bool isBackendUploadPath(const QUrl &url)
{
    const QString path = url.path().trimmed();
    return path.startsWith(QStringLiteral("/uploads/"), Qt::CaseInsensitive);
}

}
