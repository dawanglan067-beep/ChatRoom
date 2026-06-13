#pragma once

#include <QString>

class QUrl;

namespace MediaUtils
{
bool parseMediaMessageContent(const QString &content, bool *isImageOut, QString *fileNameOut, QString *urlOut,
                              qint64 *sizeOut = nullptr, QString *mimeTypeOut = nullptr);
QUrl resolveMediaUrl(const QString &rawUrl, const QString &backendBaseUrl);
bool isBackendUploadPath(const QUrl &url);
}
