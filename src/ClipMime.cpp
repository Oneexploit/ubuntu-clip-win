#include "ClipMime.h"

#include <QBuffer>
#include <QDataStream>
#include <QRegularExpression>
#include <QTextDocumentFragment>

namespace {
bool canStoreClipboardPayload(const QMimeData *mime) {
    return mime && (mime->hasText() || mime->hasHtml());
}
} // namespace

QString ClipMime::normalizedText(QString value) {
    value.replace(QChar::Null, QLatin1Char(' '));
    value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    return value;
}

QString ClipMime::plainTextFromHtml(const QString &html) {
    return normalizedText(QTextDocumentFragment::fromHtml(html).toPlainText());
}

QString ClipMime::elidedPreview(QString value, int maxChars) {
    value = normalizedText(std::move(value));
    value.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
    value = value.trimmed();
    if (value.size() > maxChars) {
        value = value.left(maxChars) + QStringLiteral("...");
    }
    return value;
}

QString ClipMime::kindLabel(const QString &kind) {
    Q_UNUSED(kind);
    return QStringLiteral("Text");
}

QByteArray ClipMime::serializeMimeBundle(const QMap<QString, QByteArray> &formats) {
    QByteArray blob;
    QDataStream stream(&blob, QIODevice::WriteOnly);
    stream.setVersion(QDataStream::Qt_6_2);
    stream << formats;
    return blob;
}

QMap<QString, QByteArray> ClipMime::deserializeMimeBundle(const QByteArray &bundle) {
    if (bundle.isEmpty()) {
        return {};
    }

    QMap<QString, QByteArray> formats;
    QBuffer buffer;
    buffer.setData(bundle);
    buffer.open(QIODevice::ReadOnly);
    QDataStream stream(&buffer);
    stream.setVersion(QDataStream::Qt_6_2);
    stream >> formats;
    return formats;
}

QStringList ClipMime::urlsFromMimeBundle(const QByteArray &bundle) {
    Q_UNUSED(bundle);
    return {};
}

std::optional<ClipPayload> ClipMime::payloadFromMimeData(const QMimeData *mime) {
    if (!mime || !canStoreClipboardPayload(mime)) {
        return std::nullopt;
    }

    QString text = mime->hasText() ? normalizedText(mime->text()) : QString();
    if (text.trimmed().isEmpty() && mime->hasHtml()) {
        text = plainTextFromHtml(mime->html());
    }

    text = normalizedText(std::move(text));
    if (text.trimmed().isEmpty()) {
        return std::nullopt;
    }

    ClipPayload payload;
    payload.kind = QStringLiteral("text");
    payload.text = text;
    return payload;
}

QMimeData *ClipMime::mimeDataFromPayload(const ClipPayload &payload) {
    auto *mime = new QMimeData();
    const QString text = normalizedText(payload.text);
    if (!text.trimmed().isEmpty()) {
        mime->setText(text);
    }
    return mime;
}
