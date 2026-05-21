#pragma once

#include <QMimeData>
#include <QString>

#include <optional>

struct ClipPayload {
    QString text;
};

namespace ClipMime {
std::optional<ClipPayload> payloadFromMimeData(const QMimeData *mime);
QMimeData *mimeDataFromPayload(const ClipPayload &payload);

QString normalizedText(QString value);
QString plainTextFromHtml(const QString &html);
QString elidedPreview(QString value, int maxChars = 260);
} // namespace ClipMime
