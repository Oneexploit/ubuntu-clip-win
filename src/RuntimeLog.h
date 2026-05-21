#pragma once

#include <QString>

namespace RuntimeLog {
void initialize();
void installQtMessageHandler();
QString logFilePath();
void write(const QString &component, const QString &message, const QString &text = QString());
} // namespace RuntimeLog
