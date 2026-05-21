#pragma once

#include "ClipMime.h"

#include <QDateTime>
#include <QString>

struct ClipItem {
    int id = -1;
    QString text;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString hash;

    bool isValid() const { return id >= 0; }
    bool hasText() const { return !text.trimmed().isEmpty(); }
    QString typeLabel() const { return QStringLiteral("Text"); }

    QString previewText() const {
        QString value = ClipMime::normalizedText(text);
        value = value.trimmed();
        if (value.isEmpty()) {
            return typeLabel();
        }
        return ClipMime::elidedPreview(value);
    }
};
