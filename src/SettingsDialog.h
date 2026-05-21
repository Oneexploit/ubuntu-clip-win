#pragma once

#include <QDialog>

class QCheckBox;
class QKeySequenceEdit;
class QLabel;
class QSpinBox;

class SettingsDialog : public QDialog {
    Q_OBJECT

public:
    explicit SettingsDialog(QWidget *parent = nullptr);

signals:
    void settingsApplied();

protected:
    void accept() override;
    void reject() override;

private:
    QCheckBox *persistentHistory_ = nullptr;
    QSpinBox *historyLimit_ = nullptr;
    QCheckBox *confirmBeforeClear_ = nullptr;
    QCheckBox *showDiagnostics_ = nullptr;
    QCheckBox *autostart_ = nullptr;
    QKeySequenceEdit *shortcutEdit_ = nullptr;
    QLabel *shortcutHint_ = nullptr;
    QLabel *environmentSummary_ = nullptr;

    bool autostartWasEnabled_ = false;
    QString shortcutDisplayAtLoad_;
};
