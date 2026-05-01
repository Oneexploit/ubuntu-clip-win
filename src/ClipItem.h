#pragma once

#include <QDateTime>
#include <QRegularExpression>
#include <QString>

struct ClipItem {
    int id = -1;
    QString type = QStringLiteral("text");
    QString text;
    bool pinned = false;
    QDateTime createdAt;
    QDateTime updatedAt;
    QString hash;

    bool isValid() const { return id >= 0; }
    bool hasImage() const { return false; }
    bool hasSvg() const { return false; }

    QString previewText() const {
        QString value = text;
        value.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
        value.replace(QLatin1Char('\r'), QLatin1Char('\n'));
        value.replace(QRegularExpression("\\n{3,}"), QStringLiteral("\n\n"));
        value = value.trimmed();
        if (value.size() > 260) {
            value = value.left(260) + QStringLiteral("…");
        }
        return value.isEmpty() ? QStringLiteral("Text") : value;
    }
};
