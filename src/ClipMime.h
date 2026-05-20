#pragma once

#include <QByteArray>
#include <QMap>
#include <QMimeData>
#include <QString>
#include <QStringList>

#include <optional>

struct ClipPayload {
    QString kind;
    QString text;
    QString html;
    QStringList urls;
    QByteArray imagePng;
    QByteArray mimeBundle;
};

namespace ClipMime {
std::optional<ClipPayload> payloadFromMimeData(const QMimeData *mime);
QMimeData *mimeDataFromPayload(const ClipPayload &payload);

QByteArray serializeMimeBundle(const QMap<QString, QByteArray> &formats);
QMap<QString, QByteArray> deserializeMimeBundle(const QByteArray &bundle);
QStringList urlsFromMimeBundle(const QByteArray &bundle);

QString normalizedText(QString value);
QString plainTextFromHtml(const QString &html);
QString elidedPreview(QString value, int maxChars = 260);
QString kindLabel(const QString &kind);
} // namespace ClipMime
