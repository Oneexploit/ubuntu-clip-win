#pragma once

#include "ClipItem.h"

#include <QClipboard>
#include <QList>
#include <QMimeData>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>

#include <optional>

class ClipboardStore : public QObject {
    Q_OBJECT

public:
    explicit ClipboardStore(QObject *parent = nullptr);
    ~ClipboardStore() override;

    bool open();
    bool isOpen() const;

    QList<ClipItem> recentItems(const QString &search = QString(), int limit = 80) const;
    std::optional<ClipItem> itemById(int id) const;
    bool copyToClipboard(const ClipItem &item);
    void reloadSettings();

public slots:
    void scheduleCaptureFromClipboard();
    void scheduleCaptureFromSelection();
    void captureFromClipboard();
    void captureFromSelection();
    void setToClipboard(const ClipItem &item);
    void deleteItem(int id);
    void togglePinned(int id);
    void clearUnpinned();

signals:
    void changed();
    void errorOccurred(const QString &message);

private:
    bool ensureSchema();
    bool migrateLegacySchemaIfNeeded();
    QString databasePath() const;
    QString hashFor(const ClipItem &item) const;
    bool tryCaptureCurrentClipboard(QClipboard::Mode mode);
    bool captureCurrentClipboardWithRetry(QClipboard::Mode mode);
    bool captureMimeData(const QMimeData *mime, QClipboard::Mode mode);
    void scheduleClipboardRetry(QClipboard::Mode mode, quint64 serial, int attempt);
    void enforceLimit();
    ClipItem readItemFromQuery(const QSqlQuery &query) const;
    QString nowIso() const;
    void applyStartupRetentionPolicy();
    void touchExistingOrInsert(const ClipItem &item);

    QSqlDatabase db_;
    QString connectionName_;
    QString lastCapturedHash_;
    quint64 pendingCaptureSerial_ = 0;
    bool suppressCapture_ = false;
};
