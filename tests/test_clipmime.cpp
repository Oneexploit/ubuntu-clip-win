#include "../src/ClipMime.h"

#include <QtTest/QTest>

#include <QImage>
#include <memory>

class ClipMimeTest : public QObject {
    Q_OBJECT

private slots:
    void preservesRichTextRoundTrip();
    void preservesImageRoundTrip();
};

void ClipMimeTest::preservesRichTextRoundTrip() {
    QMimeData mime;
    mime.setHtml(QStringLiteral("<b>Hello</b><br/>World"));
    mime.setText(QStringLiteral("Hello\nWorld"));

    const auto payload = ClipMime::payloadFromMimeData(&mime);
    QVERIFY(payload.has_value());
    QCOMPARE(payload->kind, QStringLiteral("rich-text"));
    QCOMPARE(payload->text, QStringLiteral("Hello\nWorld"));

    std::unique_ptr<QMimeData> restored(ClipMime::mimeDataFromPayload(*payload));
    QVERIFY(restored->hasHtml());
    QCOMPARE(restored->html(), QStringLiteral("<b>Hello</b><br/>World"));
    QCOMPARE(restored->text(), QStringLiteral("Hello\nWorld"));
}

void ClipMimeTest::preservesImageRoundTrip() {
    QImage image(8, 8, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::red);

    QMimeData mime;
    mime.setImageData(image);

    const auto payload = ClipMime::payloadFromMimeData(&mime);
    QVERIFY(payload.has_value());
    QCOMPARE(payload->kind, QStringLiteral("image"));
    QVERIFY(!payload->imagePng.isEmpty());

    std::unique_ptr<QMimeData> restored(ClipMime::mimeDataFromPayload(*payload));
    QVERIFY(restored->hasImage());
}

QTEST_GUILESS_MAIN(ClipMimeTest)

#include "test_clipmime.moc"
