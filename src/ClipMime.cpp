#include "ClipMime.h"

#include <QRegularExpression>
#include <QTextDocumentFragment>

#include <utility>

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
