#pragma once

#include "ClipMime.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

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
    QString typeLabel() const { return ClipMime::kindLabel(kind); }

    QString previewText() const {
        QString value = text;
        if (value.trimmed().isEmpty() && !html.trimmed().isEmpty()) {
            value = ClipMime::plainTextFromHtml(html);
        }

        value = ClipMime::normalizedText(std::move(value));
        value = value.trimmed();
        if (value.isEmpty()) {
            return typeLabel();
        }
        return ClipMime::elidedPreview(value);
    }
};
