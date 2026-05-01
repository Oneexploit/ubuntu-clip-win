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
    QString hashFor(const QString &text) const;
    bool captureMimeData(const QMimeData *mime, QClipboard::Mode mode);
    void enforceLimit(int maxItems = 120);
    ClipItem readItemFromQuery(const QSqlQuery &query) const;
    QString nowIso() const;
    void touchExistingOrInsert(const QString &text, const QString &hash);

    QSqlDatabase db_;
    QString connectionName_;
    QString lastCapturedHash_;
    bool suppressCapture_ = false;
};
