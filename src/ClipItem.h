#pragma once

#include "ClipMime.h"

#include <QByteArray>
#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QString>
#include <QStringList>
#include <QUrl>

struct ClipItem {
    int id = -1;
    QString kind = QStringLiteral("text");
    QString text;
    QString html;
    QStringList urls;
    QByteArray imagePng;
    QByteArray mimeBundle;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString hash;

    bool isValid() const { return id >= 0; }
    bool hasText() const { return !text.trimmed().isEmpty(); }
    bool hasHtml() const { return !html.trimmed().isEmpty(); }
    bool hasImage() const { return !imagePng.isEmpty(); }
    bool hasFiles() const { return kind == QStringLiteral("files"); }

    QString typeLabel() const { return ClipMime::kindLabel(kind); }

    QString previewText() const {
        if (hasFiles()) {
            QStringList names;
            names.reserve(urls.size());
            for (const QString &urlText : urls) {
                const QUrl url(urlText);
                const QString localPath = url.isLocalFile() ? url.toLocalFile() : url.toString();
                const QString fileName = QFileInfo(localPath).fileName();
                names << (fileName.isEmpty() ? localPath : fileName);
            }
            return ClipMime::elidedPreview(names.join(QStringLiteral(", ")));
        }

        if (hasImage() && !hasText()) {
            return QStringLiteral("Image clipboard item");
        }

        QString value = text;
        if (value.trimmed().isEmpty() && hasHtml()) {
            value = ClipMime::plainTextFromHtml(html);
        }

        value = ClipMime::normalizedText(std::move(value));
        value.replace(QRegularExpression(QStringLiteral("\\n{3,}")), QStringLiteral("\n\n"));
        value = value.trimmed();
        if (value.isEmpty()) {
            return typeLabel();
        }
        return ClipMime::elidedPreview(value);
    }
};
