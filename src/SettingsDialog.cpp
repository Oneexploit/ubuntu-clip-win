#include "SettingsDialog.h"

#include "AppIntegration.h"
#include "AppSettings.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QKeySequence>
#include <QKeySequenceEdit>
#include <QLabel>
#include <QMessageBox>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
QString shortcutForEditor(const QString &displayShortcut) {
    QString editable = displayShortcut;
    editable.replace(QStringLiteral("Super"), QStringLiteral("Meta"), Qt::CaseInsensitive);
    return editable;
}
} // namespace

SettingsDialog::SettingsDialog(QWidget *parent)
    : QDialog(parent),
      autostartWasEnabled_(AppIntegration::isAutostartEnabled()),
      shortcutDisplayAtLoad_(AppIntegration::configuredShortcutDisplay()) {
    setWindowTitle(QStringLiteral("Clipboard Settings"));
    setModal(true);
    resize(560, 0);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *historyGroup = new QGroupBox(QStringLiteral("History"), this);
    auto *historyLayout = new QVBoxLayout(historyGroup);
    historyLayout->setSpacing(10);

    persistentHistory_ = new QCheckBox(QStringLiteral("Keep unpinned history between restarts"), historyGroup);
    persistentHistory_->setChecked(AppSettings::persistentHistory());
    historyLayout->addWidget(persistentHistory_);

    auto *historyForm = new QFormLayout();
    historyForm->setContentsMargins(0, 0, 0, 0);
    historyForm->setSpacing(8);

    historyLimit_ = new QSpinBox(historyGroup);
    historyLimit_->setRange(20, 5000);
    historyLimit_->setValue(AppSettings::historyLimit());
    historyLimit_->setSuffix(QStringLiteral(" items"));
    historyForm->addRow(QStringLiteral("Maximum unpinned items"), historyLimit_);
    historyLayout->addLayout(historyForm);

    auto *pinNote = new QLabel(QStringLiteral("Pinned items stay at the top and survive restarts even when regular history is session-only."), historyGroup);
    pinNote->setWordWrap(true);
    historyLayout->addWidget(pinNote);

    confirmBeforeClear_ = new QCheckBox(QStringLiteral("Ask before clearing unpinned history"), historyGroup);
    confirmBeforeClear_->setChecked(AppSettings::confirmBeforeClear());
    historyLayout->addWidget(confirmBeforeClear_);

    showDiagnostics_ = new QCheckBox(QStringLiteral("Show environment diagnostics the next time the app starts"), historyGroup);
    showDiagnostics_->setChecked(AppSettings::showStartupDiagnostics());
    historyLayout->addWidget(showDiagnostics_);

    layout->addWidget(historyGroup);

    auto *integrationGroup = new QGroupBox(QStringLiteral("Startup and Shortcut"), this);
    auto *integrationLayout = new QVBoxLayout(integrationGroup);
    integrationLayout->setSpacing(10);

    environmentSummary_ = new QLabel(AppIntegration::environmentSummary(), integrationGroup);
    environmentSummary_->setWordWrap(true);
    integrationLayout->addWidget(environmentSummary_);

    autostart_ = new QCheckBox(QStringLiteral("Start in the background when I sign in"), integrationGroup);
    autostart_->setChecked(autostartWasEnabled_);
    integrationLayout->addWidget(autostart_);

    auto *shortcutForm = new QFormLayout();
    shortcutForm->setContentsMargins(0, 0, 0, 0);
    shortcutForm->setSpacing(8);

    shortcutEdit_ = new QKeySequenceEdit(integrationGroup);
    shortcutEdit_->setKeySequence(QKeySequence::fromString(shortcutForEditor(shortcutDisplayAtLoad_), QKeySequence::PortableText));
    shortcutEdit_->setEnabled(AppIntegration::isShortcutConfigAvailable());
    shortcutForm->addRow(QStringLiteral("Global shortcut"), shortcutEdit_);
    integrationLayout->addLayout(shortcutForm);

    shortcutHint_ = new QLabel(integrationGroup);
    shortcutHint_->setWordWrap(true);
    if (AppIntegration::isShortcutConfigAvailable()) {
        shortcutHint_->setText(QStringLiteral("Press a single combination such as Ctrl+Alt+V. This updates the GNOME Shell extension shortcut."));
    } else {
        shortcutHint_->setText(QStringLiteral("Shortcut editing is unavailable because the GNOME extension schema could not be found from this app."));
    }
    integrationLayout->addWidget(shortcutHint_);

    layout->addWidget(integrationGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &SettingsDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &SettingsDialog::reject);
    layout->addWidget(buttons);
}

void SettingsDialog::accept() {
    QString errorMessage;
    const bool autostartChanged = autostart_->isChecked() != autostartWasEnabled_;
    const bool shortcutAvailable = AppIntegration::isShortcutConfigAvailable();
    QString shortcutText;
    if (shortcutAvailable) {
        shortcutText = AppIntegration::normalizeShortcutDisplay(shortcutEdit_->keySequence().toString(QKeySequence::PortableText));
        if (shortcutText.isEmpty()) {
            shortcutText = AppIntegration::normalizeShortcutDisplay(shortcutEdit_->keySequence().toString(QKeySequence::NativeText));
        }
    }
    const QString originalShortcut = AppIntegration::normalizeShortcutDisplay(shortcutDisplayAtLoad_);
    const bool shortcutChanged = shortcutAvailable && shortcutText != originalShortcut;

    if (shortcutAvailable && shortcutText.isEmpty()) {
        QMessageBox::warning(this,
                             QStringLiteral("Clipboard Settings"),
                             QStringLiteral("Please press one valid shortcut combination before saving."));
        return;
    }

    if (shortcutChanged) {
        if (!AppIntegration::setConfiguredShortcutDisplay(shortcutText, &errorMessage)) {
            QMessageBox::warning(this, QStringLiteral("Clipboard Settings"), errorMessage);
            return;
        }
    }

    if (autostartChanged) {
        if (!AppIntegration::setAutostartEnabled(autostart_->isChecked(), &errorMessage)) {
            if (shortcutChanged) {
                QString rollbackError;
                AppIntegration::setConfiguredShortcutDisplay(originalShortcut, &rollbackError);
            }
            QMessageBox::warning(this, QStringLiteral("Clipboard Settings"), errorMessage);
            return;
        }
    }

    AppSettings::setPersistentHistory(persistentHistory_->isChecked());
    AppSettings::setHistoryLimit(historyLimit_->value());
    AppSettings::setConfirmBeforeClear(confirmBeforeClear_->isChecked());
    AppSettings::setShowStartupDiagnostics(showDiagnostics_->isChecked());

    emit settingsApplied();
    QDialog::accept();
}
