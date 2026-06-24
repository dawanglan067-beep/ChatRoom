#include <QtTest>
#include <QUrl>

#include "mediautils.h"

class MediaUtilsTest : public QObject
{
    Q_OBJECT

private slots:
    void parseImageMessageWithChineseTag();
    void parseImageMessageWithEnglishTag();
    void parseFileMessageWithChineseTag();
    void parseFileMessageWithUrlPrefix();
    void parseImageWithFileName();
    void parseImageWithSizeAndMime();
    void parseEmptyContentReturnsFalse();
    void parseSingleLineReturnsFalse();
    void parseUnknownTagReturnsFalse();
    void parseMissingUrlReturnsFalse();
    void resolveRelativePath();
    void resolveAbsoluteHttpUrl();
    void resolveAbsoluteHttpsUrl();
    void resolveInvalidBaseUrlReturnsEmpty();
    void resolveEmptyUrlReturnsEmpty();
    void isBackendUploadPathTrue();
    void isBackendUploadPathFalse();
    void isBackendUploadPathCaseInsensitive();
};

void MediaUtilsTest::parseImageMessageWithChineseTag()
{
    bool isImage = false;
    QString fileName, url;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[图片] avatar.png\n/uploads/abc.png"),
        &isImage, &fileName, &url));
    QVERIFY(isImage);
    QCOMPARE(fileName, QStringLiteral("avatar.png"));
    QCOMPARE(url, QStringLiteral("/uploads/abc.png"));
}

void MediaUtilsTest::parseImageMessageWithEnglishTag()
{
    bool isImage = false;
    QString fileName, url;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[image] photo.jpg\n/uploads/def.jpg"),
        &isImage, &fileName, &url));
    QVERIFY(isImage);
    QCOMPARE(fileName, QStringLiteral("photo.jpg"));
}

void MediaUtilsTest::parseFileMessageWithChineseTag()
{
    bool isImage = true;
    QString fileName, url;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[文件] report.pdf\n/uploads/report.pdf"),
        &isImage, &fileName, &url));
    QVERIFY(!isImage);
    QCOMPARE(fileName, QStringLiteral("report.pdf"));
    QCOMPARE(url, QStringLiteral("/uploads/report.pdf"));
}

void MediaUtilsTest::parseFileMessageWithUrlPrefix()
{
    bool isImage = false;
    QString fileName, url;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[File] doc.txt\nURL: /uploads/doc.txt"),
        &isImage, &fileName, &url));
    QVERIFY(!isImage);
    QCOMPARE(url, QStringLiteral("/uploads/doc.txt"));
}

void MediaUtilsTest::parseImageWithFileName()
{
    bool isImage = false;
    QString fileName;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[Image]\n/uploads/no-name.png"),
        &isImage, &fileName, nullptr));
    QVERIFY(isImage);
    QCOMPARE(fileName, QStringLiteral("[Image]"));
}

void MediaUtilsTest::parseImageWithSizeAndMime()
{
    bool isImage = false;
    QString fileName, url, mimeType;
    qint64 size = -1;
    QVERIFY(MediaUtils::parseMediaMessageContent(
        QStringLiteral("[图片] test.png\n/uploads/test.png\n大小: 12345\n类型: image/png"),
        &isImage, &fileName, &url, &size, &mimeType));
    QVERIFY(isImage);
    QCOMPARE(size, 12345LL);
    QCOMPARE(mimeType, QStringLiteral("image/png"));
}

void MediaUtilsTest::parseEmptyContentReturnsFalse()
{
    QVERIFY(!MediaUtils::parseMediaMessageContent(QString(), nullptr, nullptr, nullptr));
    QVERIFY(!MediaUtils::parseMediaMessageContent(QStringLiteral(""), nullptr, nullptr, nullptr));
    QVERIFY(!MediaUtils::parseMediaMessageContent(QStringLiteral("   "), nullptr, nullptr, nullptr));
}

void MediaUtilsTest::parseSingleLineReturnsFalse()
{
    QVERIFY(!MediaUtils::parseMediaMessageContent(QStringLiteral("[图片] test.png"), nullptr, nullptr, nullptr));
}

void MediaUtilsTest::parseUnknownTagReturnsFalse()
{
    QVERIFY(!MediaUtils::parseMediaMessageContent(
        QStringLiteral("[unknown] file.txt\n/uploads/file.txt"), nullptr, nullptr, nullptr));
}

void MediaUtilsTest::parseMissingUrlReturnsFalse()
{
    QVERIFY(!MediaUtils::parseMediaMessageContent(
        QStringLiteral("[图片] test.png\n"), nullptr, nullptr, nullptr));
}

void MediaUtilsTest::resolveRelativePath()
{
    const QUrl result = MediaUtils::resolveMediaUrl(
        QStringLiteral("/uploads/test.png"), QStringLiteral("http://127.0.0.1:3000"));
    QCOMPARE(result.toString(), QStringLiteral("http://127.0.0.1:3000/uploads/test.png"));
}

void MediaUtilsTest::resolveAbsoluteHttpUrl()
{
    const QUrl result = MediaUtils::resolveMediaUrl(
        QStringLiteral("http://example.com/img.png"), QStringLiteral("http://127.0.0.1:3000"));
    QCOMPARE(result.toString(), QStringLiteral("http://example.com/img.png"));
}

void MediaUtilsTest::resolveAbsoluteHttpsUrl()
{
    const QUrl result = MediaUtils::resolveMediaUrl(
        QStringLiteral("https://cdn.example.com/file.pdf"), QStringLiteral("http://127.0.0.1:3000"));
    QCOMPARE(result.toString(), QStringLiteral("https://cdn.example.com/file.pdf"));
}

void MediaUtilsTest::resolveInvalidBaseUrlReturnsEmpty()
{
    const QUrl result = MediaUtils::resolveMediaUrl(
        QStringLiteral("/uploads/test.png"), QString());
    QVERIFY(!result.isValid());
}

void MediaUtilsTest::resolveEmptyUrlReturnsEmpty()
{
    const QUrl result = MediaUtils::resolveMediaUrl(QString(), QString());
    QVERIFY(!result.isValid());
}

void MediaUtilsTest::isBackendUploadPathTrue()
{
    QVERIFY(MediaUtils::isBackendUploadPath(QUrl(QStringLiteral("http://example.com/uploads/file.png"))));
    QVERIFY(MediaUtils::isBackendUploadPath(QUrl(QStringLiteral("http://example.com/uploads/abc123.pdf"))));
}

void MediaUtilsTest::isBackendUploadPathFalse()
{
    QVERIFY(!MediaUtils::isBackendUploadPath(QUrl(QStringLiteral("http://example.com/images/test.png"))));
    QVERIFY(!MediaUtils::isBackendUploadPath(QUrl(QStringLiteral("http://example.com/"))));
}

void MediaUtilsTest::isBackendUploadPathCaseInsensitive()
{
    QVERIFY(MediaUtils::isBackendUploadPath(QUrl(QStringLiteral("http://example.com/Uploads/file.png"))));
}

QTEST_MAIN(MediaUtilsTest)
#include "mediautils_test.moc"
