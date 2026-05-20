#include "ClipMime.h"

#include <QBuffer>
#include <QDataStream>
#include <QImage>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrl>

namespace {
constexpr int kMaxFormatBytes = 4 * 1024 * 1024;
constexpr int kMaxMimeBundleBytes = 8 * 1024 * 1024;

bool formatListContains(const QStringList &formats, const QString &needle) {
    const QString loweredNeedle = needle.toLower();
    for (const QString &format : formats) {
        if (format.toLower().contains(loweredNeedle)) {
            return true;
        }
    }
    return false;
}

bool canStoreClipboardPayload(const QMimeData *mime) {
    if (!mime) {
        return false;
    }

    const QStringList formats = mime->formats();
    return mime->hasText()
        || mime->hasHtml()
        || mime->hasUrls()
        || mime->hasImage()
        || mime->hasFormat(QStringLiteral("x-special/gnome-copied-files"))
        || formatListContains(formats, QStringLiteral("image/"))
        || formatListContains(formats, QStringLiteral("rtf"));
}

QByteArray imageToPng(const QImage &image) {
    if (image.isNull()) {
        return {};
    }

    QByteArray pngBytes;
    QBuffer buffer(&pngBytes);
    if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
        return {};
    }
    return pngBytes;
}

QStringList urlStrings(const QList<QUrl> &urls) {
    QStringList values;
    values.reserve(urls.size());
    for (const QUrl &url : urls) {
        values << url.toString();
    }
    return values;
}

QStringList displayPaths(const QList<QUrl> &urls) {
    QStringList values;
    values.reserve(urls.size());
    for (const QUrl &url : urls) {
        values << (url.isLocalFile() ? url.toLocalFile() : url.toString());
    }
    return values;
}

QStringList parseUriList(const QByteArray &raw) {
    QStringList urls;
    const QString text = QString::fromUtf8(raw);
    for (const QString &line : text.split(QRegularExpression(QStringLiteral("[\r\n]+")), Qt::SkipEmptyParts)) {
        const QString trimmed = line.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith(QLatin1Char('#'))) {
            continue;
        }
        urls << trimmed;
    }
    return urls;
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
    if (kind == QStringLiteral("image")) {
        return QStringLiteral("Image");
    }
    if (kind == QStringLiteral("files")) {
        return QStringLiteral("Files");
    }
    if (kind == QStringLiteral("rich-text")) {
        return QStringLiteral("Rich text");
    }
    if (kind == QStringLiteral("html")) {
        return QStringLiteral("HTML");
    }
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
    const QMap<QString, QByteArray> formats = deserializeMimeBundle(bundle);
    if (formats.contains(QStringLiteral("text/uri-list"))) {
        return parseUriList(formats.value(QStringLiteral("text/uri-list")));
    }
    if (formats.contains(QStringLiteral("x-special/gnome-copied-files"))) {
        QStringList values = parseUriList(formats.value(QStringLiteral("x-special/gnome-copied-files")));
        if (!values.isEmpty() && (values.first() == QStringLiteral("copy") || values.first() == QStringLiteral("cut"))) {
            values.removeFirst();
        }
        return values;
    }
    return {};
}

std::optional<ClipPayload> ClipMime::payloadFromMimeData(const QMimeData *mime) {
    if (!mime || !canStoreClipboardPayload(mime)) {
        return std::nullopt;
    }

    QMap<QString, QByteArray> rawFormats;
    int totalBytes = 0;
    auto addFormat = [&](const QString &format, const QByteArray &bytes) {
        if (format.isEmpty() || bytes.isEmpty() || bytes.size() > kMaxFormatBytes) {
            return;
        }
        if (totalBytes + bytes.size() > kMaxMimeBundleBytes) {
            return;
        }
        if (!rawFormats.contains(format)) {
            rawFormats.insert(format, bytes);
            totalBytes += bytes.size();
        }
    };

    for (const QString &format : mime->formats()) {
        addFormat(format, mime->data(format));
    }

    QString text = mime->hasText() ? normalizedText(mime->text()) : QString();
    QString html = mime->hasHtml() ? mime->html() : QString();
    const QList<QUrl> urlList = mime->hasUrls() ? mime->urls() : QList<QUrl>();
    bool hasLocalFileUrls = !urlList.isEmpty();
    for (const QUrl &url : urlList) {
        if (!url.isLocalFile() && url.scheme().compare(QStringLiteral("file"), Qt::CaseInsensitive) != 0) {
            hasLocalFileUrls = false;
            break;
        }
    }

    if (!text.trimmed().isEmpty()) {
        addFormat(QStringLiteral("text/plain"), text.toUtf8());
    }
    if (!html.trimmed().isEmpty()) {
        addFormat(QStringLiteral("text/html"), html.toUtf8());
        if (text.trimmed().isEmpty()) {
            text = plainTextFromHtml(html);
        }
    }
    if (!urlList.isEmpty()) {
        const QStringList serializedUrls = urlStrings(urlList);
        addFormat(QStringLiteral("text/uri-list"), serializedUrls.join(QStringLiteral("\r\n")).toUtf8());
        if (text.trimmed().isEmpty()) {
            text = normalizedText(displayPaths(urlList).join(QStringLiteral("\n")));
        }
    }

    QByteArray imagePng;
    if (mime->hasImage()) {
        imagePng = imageToPng(qvariant_cast<QImage>(mime->imageData()));
    }
    if (imagePng.isEmpty()) {
        for (auto it = rawFormats.cbegin(); it != rawFormats.cend(); ++it) {
            if (!it.key().startsWith(QStringLiteral("image/"))) {
                continue;
            }
            QImage image;
            image.loadFromData(it.value());
            imagePng = imageToPng(image);
            if (!imagePng.isEmpty()) {
                break;
            }
        }
    }
    if (!imagePng.isEmpty()) {
        addFormat(QStringLiteral("image/png"), imagePng);
    }

    QString kind = QStringLiteral("text");
    if (!imagePng.isEmpty()) {
        kind = QStringLiteral("image");
        if (text.trimmed().isEmpty()) {
            text = QStringLiteral("Image clipboard item");
        }
    } else if (mime->hasFormat(QStringLiteral("x-special/gnome-copied-files")) || hasLocalFileUrls) {
        kind = QStringLiteral("files");
    } else if (!html.trimmed().isEmpty()) {
        kind = QStringLiteral("rich-text");
    } else if (!text.trimmed().isEmpty()) {
        kind = QStringLiteral("text");
    } else {
        return std::nullopt;
    }

    if (rawFormats.isEmpty() && text.trimmed().isEmpty() && imagePng.isEmpty()) {
        return std::nullopt;
    }

    ClipPayload payload;
    payload.kind = kind;
    payload.text = text;
    payload.html = html;
    payload.urls = !urlList.isEmpty() ? urlStrings(urlList) : QStringList();
    payload.imagePng = imagePng;
    payload.mimeBundle = serializeMimeBundle(rawFormats);
    return payload;
}

QMimeData *ClipMime::mimeDataFromPayload(const ClipPayload &payload) {
    auto *mime = new QMimeData();
    const QMap<QString, QByteArray> rawFormats = deserializeMimeBundle(payload.mimeBundle);
    for (auto it = rawFormats.cbegin(); it != rawFormats.cend(); ++it) {
        mime->setData(it.key(), it.value());
    }

    if (!payload.text.isEmpty()) {
        mime->setText(payload.text);
    }
    if (!payload.html.isEmpty()) {
        mime->setHtml(payload.html);
    }
    if (!payload.urls.isEmpty()) {
        QList<QUrl> urls;
        urls.reserve(payload.urls.size());
        for (const QString &urlText : payload.urls) {
            urls << QUrl(urlText);
        }
        mime->setUrls(urls);
    }
    if (!payload.imagePng.isEmpty()) {
        QImage image;
        image.loadFromData(payload.imagePng, "PNG");
        if (!image.isNull()) {
            mime->setImageData(image);
        }
        if (!rawFormats.contains(QStringLiteral("image/png"))) {
            mime->setData(QStringLiteral("image/png"), payload.imagePng);
        }
    }

    return mime;
}
