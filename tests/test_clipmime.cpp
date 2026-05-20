#include "../src/ClipMime.h"

#include <QtTest/QTest>

#include <QImage>
#include <QMimeData>
#include <memory>

class ClipMimeTest : public QObject {
    Q_OBJECT

private slots:
    void capturesPlainTextFromRichText();
    void ignoresImageOnlyPayloads();
};

void ClipMimeTest::capturesPlainTextFromRichText() {
    QMimeData mime;
    mime.setHtml(QStringLiteral("<b>Hello</b><br/>World"));
    mime.setText(QStringLiteral("Hello\nWorld"));

    const auto payload = ClipMime::payloadFromMimeData(&mime);
    QVERIFY(payload.has_value());
    QCOMPARE(payload->kind, QStringLiteral("text"));
    QCOMPARE(payload->text, QStringLiteral("Hello\nWorld"));

    std::unique_ptr<QMimeData> restored(ClipMime::mimeDataFromPayload(*payload));
    QVERIFY(restored->hasText());
    QVERIFY(!restored->hasHtml());
    QCOMPARE(restored->text(), QStringLiteral("Hello\nWorld"));
}

void ClipMimeTest::ignoresImageOnlyPayloads() {
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);

    QMimeData mime;
    mime.setImageData(image);

    const auto payload = ClipMime::payloadFromMimeData(&mime);
    QVERIFY(!payload.has_value());
}

QTEST_APPLESS_MAIN(ClipMimeTest)

#include "test_clipmime.moc"
