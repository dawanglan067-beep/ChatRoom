#include "chatutils.h"

#include <QCryptographicHash>
#include <QRandomGenerator>
#include <QSettings>

#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#pragma comment(lib, "crypt32.lib")
#endif

namespace ChatUtils::SecureStorage
{

#ifdef Q_OS_WIN

QByteArray encryptString(const QString &plaintext)
{
    if (plaintext.isEmpty()) {
        return QByteArray();
    }

    const QByteArray utf16Bytes = plaintext.toUtf8();
    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(utf16Bytes.constData()));
    inputBlob.cbData = static_cast<DWORD>(utf16Bytes.size());

    DATA_BLOB outputBlob;
    if (!CryptProtectData(&inputBlob, L"ChatRoomToken", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
        return QByteArray();
    }

    QByteArray result(reinterpret_cast<const char *>(outputBlob.pbData), static_cast<int>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);
    return result;
}

QString decryptString(const QByteArray &ciphertext)
{
    if (ciphertext.isEmpty()) {
        return QString();
    }

    DATA_BLOB inputBlob;
    inputBlob.pbData = reinterpret_cast<BYTE *>(const_cast<char *>(ciphertext.constData()));
    inputBlob.cbData = static_cast<DWORD>(ciphertext.size());

    DATA_BLOB outputBlob;
    LPWSTR description = nullptr;
    if (!CryptUnprotectData(&inputBlob, &description, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &outputBlob)) {
        return QString();
    }

    const QString result = QString::fromUtf8(reinterpret_cast<const char *>(outputBlob.pbData),
                                              static_cast<int>(outputBlob.cbData));
    LocalFree(outputBlob.pbData);
    if (description) {
        LocalFree(description);
    }
    return result;
}

#else

static QByteArray deriveKey()
{
    const QByteArray machineId = QSysInfo::machineUniqueId();
    return QCryptographicHash::hash(machineId, QCryptographicHash::Sha256).left(32);
}

QByteArray encryptString(const QString &plaintext)
{
    if (plaintext.isEmpty()) {
        return QByteArray();
    }

    const QByteArray key = deriveKey();
    const QByteArray data = plaintext.toUtf8();
    quint64 ivValue = QRandomGenerator::global()->generate64();
    const QByteArray iv = QByteArray::fromRawData(reinterpret_cast<const char *>(&ivValue), sizeof(ivValue));

    QByteArray result;
    result.reserve(iv.size() + data.size());
    result.append(iv);

    for (int i = 0; i < data.size(); ++i) {
        result.append(data.at(i) ^ key.at(i % key.size()) ^ iv.at(i % iv.size()));
    }

    return result.toBase64();
}

QString decryptString(const QByteArray &ciphertext)
{
    if (ciphertext.isEmpty()) {
        return QString();
    }

    const QByteArray key = deriveKey();
    const QByteArray data = QByteArray::fromBase64(ciphertext);
    if (data.size() < 8) {
        return QString();
    }

    const QByteArray iv = data.left(8);
    const QByteArray encrypted = data.mid(8);

    QByteArray result;
    result.reserve(encrypted.size());

    for (int i = 0; i < encrypted.size(); ++i) {
        result.append(encrypted.at(i) ^ key.at(i % key.size()) ^ iv.at(i % iv.size()));
    }

    return QString::fromUtf8(result);
}

#endif

void secureSetValue(const QString &key, const QString &value)
{
    QSettings settings;
    const QByteArray encrypted = encryptString(value);
    if (encrypted.isEmpty()) {
        settings.remove(key);
    } else {
        settings.setValue(key + QStringLiteral("_enc"), encrypted);
        settings.remove(key);
    }
    settings.sync();
}

QString secureGetValue(const QString &key)
{
    QSettings settings;

    const QByteArray encrypted = settings.value(key + QStringLiteral("_enc")).toByteArray();
    if (!encrypted.isEmpty()) {
        return decryptString(encrypted);
    }

    const QString plaintext = settings.value(key).toString();
    if (!plaintext.isEmpty()) {
        secureSetValue(key, plaintext);
        return plaintext;
    }

    return QString();
}

}
