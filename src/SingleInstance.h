#pragma once

#include <QLocalServer>
#include <QObject>
#include <QString>

class SingleInstance : public QObject {
    Q_OBJECT

public:
    explicit SingleInstance(QObject *parent = nullptr);
    ~SingleInstance() override;

    static QString socketName();
    static bool sendMessage(const QString &message);
    bool listen();

signals:
    void showRequested();
    void showRequestedForWindow(const QString &targetWindowId);
    void settingsRequested();
    void messageReceived(const QString &message);

private slots:
    void handleNewConnection();

private:
    QLocalServer server_;
};
