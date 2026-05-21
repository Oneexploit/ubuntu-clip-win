#include "AppIntegration.h"

#include "AppSettings.h"
#include "PasteController.h"
#include "RuntimeLog.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSaveFile>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>

namespace {
constexpr auto kExtensionUuid = "ubuntu-clip-win@amirhosein.local";
constexpr auto kShortcutSchema = "org.gnome.shell.extensions.ubuntu-clip-win";
constexpr auto kShortcutKey = "show-ubuntu-clip-win";

QStringList candidateSchemaDirs() {
    const QString uuid = QString::fromLatin1(kExtensionUuid);
    return {
        QDir::homePath() + QStringLiteral("/.local/share/gnome-shell/extensions/") + uuid + QStringLiteral("/schemas"),
        QStringLiteral("/usr/local/share/gnome-shell/extensions/") + uuid + QStringLiteral("/schemas"),
        QStringLiteral("/usr/share/gnome-shell/extensions/") + uuid + QStringLiteral("/schemas")
    };
}

QString schemaDir() {
    for (const QString &dirPath : candidateSchemaDirs()) {
        const QFileInfo compiled(QDir(dirPath).filePath(QStringLiteral("gschemas.compiled")));
        const QFileInfo xml(QDir(dirPath).filePath(QStringLiteral("org.gnome.shell.extensions.ubuntu-clip-win.gschema.xml")));
        if (compiled.exists() || xml.exists()) {
            return dirPath;
        }
    }
    return {};
}

bool runProcess(const QString &program,
                const QStringList &arguments,
                QString *standardOutput = nullptr,
                QString *standardError = nullptr,
                int timeoutMs = 2000) {
    RuntimeLog::write(QStringLiteral("AppIntegration"),
                      QStringLiteral("run-process start program=%1 args=%2 timeoutMs=%3")
                          .arg(program)
                          .arg(arguments.join(QStringLiteral(" ")))
                          .arg(timeoutMs));
    QProcess process;
    process.start(program, arguments);
    if (!process.waitForFinished(timeoutMs)) {
        process.kill();
        process.waitForFinished(250);
        if (standardError) {
            *standardError = QStringLiteral("The command timed out.");
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"),
                          QStringLiteral("run-process timeout program=%1 args=%2").arg(program).arg(arguments.join(QStringLiteral(" "))));
        return false;
    }

    const QString stdoutText = QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed();
    const QString stderrText = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();
    if (standardOutput) {
        *standardOutput = stdoutText;
    }
    if (standardError) {
        *standardError = stderrText;
    }
    const bool ok = process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
    RuntimeLog::write(QStringLiteral("AppIntegration"),
                      QStringLiteral("run-process finished ok=%1 exitCode=%2 stdout=%3 stderr=%4")
                          .arg(ok ? QStringLiteral("true") : QStringLiteral("false"))
                          .arg(process.exitCode())
                          .arg(stdoutText)
                          .arg(stderrText));
    return ok;
}

QString gsettingsPath() {
    return QStandardPaths::findExecutable(QStringLiteral("gsettings"));
}

QString gnomeExtensionsPath() {
    return QStandardPaths::findExecutable(QStringLiteral("gnome-extensions"));
}

QString defaultShortcutDisplay() {
    return QStringLiteral("Ctrl+Alt+V");
}

QString normalizeKeyToken(QString token) {
    token = token.trimmed();
    if (token.isEmpty()) {
        return {};
    }

    if (token.compare(QStringLiteral("ctrl"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("control"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("primary"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Ctrl");
    }
    if (token.compare(QStringLiteral("alt"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Alt");
    }
    if (token.compare(QStringLiteral("shift"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Shift");
    }
    if (token.compare(QStringLiteral("meta"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("super"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("win"), Qt::CaseInsensitive) == 0
        || token.compare(QStringLiteral("windows"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Super");
    }
    if (token.compare(QStringLiteral("esc"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Escape");
    }
    if (token.compare(QStringLiteral("del"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Delete");
    }
    if (token.compare(QStringLiteral("ins"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("Insert");
    }
    if (token.compare(QStringLiteral("pgup"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("PageUp");
    }
    if (token.compare(QStringLiteral("pgdown"), Qt::CaseInsensitive) == 0) {
        return QStringLiteral("PageDown");
    }
    if (token.size() == 1) {
        return token.toUpper();
    }
    if (token.startsWith(QLatin1Char('F')) && token.size() > 1) {
        bool ok = false;
        token.mid(1).toInt(&ok);
        if (ok) {
            return token.toUpper();
        }
    }

    return token.left(1).toUpper() + token.mid(1);
}

QString canonicalizeDisplayShortcut(QString displayShortcut) {
    displayShortcut = displayShortcut.trimmed();
    if (displayShortcut.isEmpty()) {
        return {};
    }

    QStringList parts = displayShortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    bool hasCtrl = false;
    bool hasAlt = false;
    bool hasShift = false;
    bool hasSuper = false;
    QString keyToken;

    for (QString part : parts) {
        const QString token = normalizeKeyToken(part);
        if (token.isEmpty()) {
            return {};
        }
        if (token == QStringLiteral("Ctrl")) {
            hasCtrl = true;
            continue;
        }
        if (token == QStringLiteral("Alt")) {
            hasAlt = true;
            continue;
        }
        if (token == QStringLiteral("Shift")) {
            hasShift = true;
            continue;
        }
        if (token == QStringLiteral("Super")) {
            hasSuper = true;
            continue;
        }
        if (!keyToken.isEmpty()) {
            return {};
        }
        keyToken = token;
    }

    if (keyToken.isEmpty()) {
        return {};
    }

    QStringList ordered;
    if (hasCtrl) {
        ordered << QStringLiteral("Ctrl");
    }
    if (hasAlt) {
        ordered << QStringLiteral("Alt");
    }
    if (hasShift) {
        ordered << QStringLiteral("Shift");
    }
    if (hasSuper) {
        ordered << QStringLiteral("Super");
    }
    ordered << keyToken;
    return ordered.join(QLatin1Char('+'));
}

QString shortcutValueToDisplay(QString rawValue) {
    rawValue.remove(QLatin1Char('['));
    rawValue.remove(QLatin1Char(']'));
    rawValue.remove(QLatin1Char('\''));
    rawValue.remove(QLatin1Char('"'));
    rawValue = rawValue.trimmed();
    if (rawValue.isEmpty()) {
        return defaultShortcutDisplay();
    }

    rawValue.replace(QStringLiteral("<Primary>"), QStringLiteral("Ctrl+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Control>"), QStringLiteral("Ctrl+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Ctrl>"), QStringLiteral("Ctrl+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Super>"), QStringLiteral("Super+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Meta>"), QStringLiteral("Super+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Alt>"), QStringLiteral("Alt+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("<Shift>"), QStringLiteral("Shift+"), Qt::CaseInsensitive);
    rawValue.replace(QStringLiteral("++"), QStringLiteral("+"));
    return canonicalizeDisplayShortcut(rawValue);
}

QString displayShortcutToAccelerator(QString displayShortcut) {
    displayShortcut = canonicalizeDisplayShortcut(displayShortcut);
    if (displayShortcut.isEmpty()) {
        return {};
    }

    QStringList parts = displayShortcut.split(QLatin1Char('+'), Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        return {};
    }

    QString accelerator;
    for (int index = 0; index < parts.size(); ++index) {
        QString token = parts.at(index).trimmed();
        if (token.compare(QStringLiteral("Ctrl"), Qt::CaseInsensitive) == 0
            || token.compare(QStringLiteral("Control"), Qt::CaseInsensitive) == 0) {
            accelerator += QStringLiteral("<Control>");
            continue;
        }
        if (token.compare(QStringLiteral("Alt"), Qt::CaseInsensitive) == 0) {
            accelerator += QStringLiteral("<Alt>");
            continue;
        }
        if (token.compare(QStringLiteral("Shift"), Qt::CaseInsensitive) == 0) {
            accelerator += QStringLiteral("<Shift>");
            continue;
        }
        if (token.compare(QStringLiteral("Meta"), Qt::CaseInsensitive) == 0
            || token.compare(QStringLiteral("Super"), Qt::CaseInsensitive) == 0
            || token.compare(QStringLiteral("Win"), Qt::CaseInsensitive) == 0) {
            accelerator += QStringLiteral("<Super>");
            continue;
        }

        if (index != parts.size() - 1) {
            return {};
        }

        if (token.size() == 1) {
            accelerator += token.toLower();
        } else {
            accelerator += token;
        }
    }

    return accelerator;
}

QString autostartFilePath() {
    const QString configHome = QStandardPaths::writableLocation(QStandardPaths::ConfigLocation);
    return QDir(configHome).filePath(QStringLiteral("autostart/ubuntu-clip-win.desktop"));
}

QString autostartDesktopEntry() {
    const QString executable = QCoreApplication::applicationFilePath();
    return QStringLiteral(
        "[Desktop Entry]\n"
        "Type=Application\n"
        "Name=Clipboard History\n"
        "Comment=Start clipboard history in the background\n"
        "Exec=\"%1\" --background\n"
        "Icon=ubuntu-clip-win\n"
        "Terminal=false\n"
        "X-GNOME-Autostart-enabled=true\n")
        .arg(executable);
}

void refreshGnomeExtensionShortcutBinding() {
    const QString gnomeExtensions = gnomeExtensionsPath();
    if (gnomeExtensions.isEmpty()) {
        return;
    }

    const QString uuid = QString::fromLatin1(kExtensionUuid);
    runProcess(gnomeExtensions, {QStringLiteral("disable"), uuid}, nullptr, nullptr, 4000);
    runProcess(gnomeExtensions, {QStringLiteral("enable"), uuid}, nullptr, nullptr, 4000);
}

bool isGnomeShortcutBackendAvailable() {
    return !schemaDir().isEmpty() && !gsettingsPath().isEmpty();
}
} // namespace

QString AppIntegration::environmentSummary() {
    if (PasteController::isWaylandSession()) {
        return QStringLiteral("Wayland session: selecting an item copies it to the clipboard. Paste it with Ctrl+V in the target app.");
    }
    if (PasteController::canAutoPaste()) {
        return QStringLiteral("X11 session: Enter pastes directly into the previous application.");
    }
    if (PasteController::isX11Session()) {
        return QStringLiteral("X11 session without xdotool: items are copied, then you paste them manually with Ctrl+V.");
    }
    return QStringLiteral("Unknown desktop session: clipboard capture works, but paste automation may depend on your environment.");
}

QStringList AppIntegration::diagnosticsMessages() {
    QStringList messages;
    if (PasteController::isWaylandSession()) {
        messages << QStringLiteral("Wayland is active. Clipboard items are restored to the clipboard, then you paste them with Ctrl+V in the target app.");
    } else if (PasteController::isX11Session() && !PasteController::canAutoPaste()) {
        messages << QStringLiteral("X11 is active but xdotool was not found. Install xdotool to enable one-key paste.");
    }

    if (PasteController::isX11Session()) {
        messages << QStringLiteral("X11 is active. The app handles the global shortcut directly on Linux without needing a GNOME extension.");
    } else if (!isGnomeShortcutBackendAvailable()) {
        messages << QStringLiteral("Wayland needs the GNOME shortcut extension for a global hotkey. You can still open the window with ubuntu-clip-win --show.");
    }
    return messages;
}

bool AppIntegration::isShortcutConfigAvailable() {
    return PasteController::isX11Session() || isGnomeShortcutBackendAvailable();
}

QString AppIntegration::configuredShortcutDisplay() {
    const QString storedShortcut = normalizeShortcutDisplay(AppSettings::globalShortcut());
    if (!storedShortcut.isEmpty()) {
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("configured-shortcut source=settings value=%1").arg(storedShortcut));
        return storedShortcut;
    }

    if (!isGnomeShortcutBackendAvailable()) {
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("configured-shortcut source=default value=%1").arg(defaultShortcutDisplay()));
        return defaultShortcutDisplay();
    }

    QString output;
    const bool ok = runProcess(gsettingsPath(),
                               {
                                   QStringLiteral("--schemadir"),
                                   schemaDir(),
                                   QStringLiteral("get"),
                                   QString::fromLatin1(kShortcutSchema),
                                   QString::fromLatin1(kShortcutKey)
                               },
                               &output);
    const QString shortcut = ok ? shortcutValueToDisplay(output) : QString();
    if (!shortcut.isEmpty()) {
        AppSettings::setGlobalShortcut(shortcut);
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("configured-shortcut source=gsettings value=%1").arg(shortcut));
        return shortcut;
    }
    RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("configured-shortcut fallback-default value=%1").arg(defaultShortcutDisplay()));
    return defaultShortcutDisplay();
}

QString AppIntegration::normalizeShortcutDisplay(const QString &displayShortcut) {
    QString normalized = displayShortcut.trimmed();
    if (normalized.isEmpty()) {
        return {};
    }

    if (normalized.contains(QLatin1Char(','))) {
        normalized = normalized.section(QLatin1Char(','), 0, 0).trimmed();
    }
    if (normalized.contains(QLatin1Char('<')) || normalized.contains(QLatin1Char('['))) {
        return shortcutValueToDisplay(normalized);
    }

    return canonicalizeDisplayShortcut(normalized);
}

bool AppIntegration::setConfiguredShortcutDisplay(const QString &displayShortcut, QString *errorMessage) {
    const QString normalizedShortcut = normalizeShortcutDisplay(displayShortcut);
    if (normalizedShortcut.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The shortcut format is invalid.");
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut failed reason=invalid-format input=%1").arg(displayShortcut));
        return false;
    }

    AppSettings::setGlobalShortcut(normalizedShortcut);
    RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut normalized=%1").arg(normalizedShortcut));

    if (PasteController::isX11Session()) {
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut success x11-only shortcut=%1").arg(normalizedShortcut));
        return true;
    }

    if (!isGnomeShortcutBackendAvailable()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("Wayland global shortcuts need the GNOME extension schema to be installed and active.");
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut failed reason=no-wayland-backend shortcut=%1").arg(normalizedShortcut));
        return false;
    }

    const QString accelerator = displayShortcutToAccelerator(normalizedShortcut);
    if (accelerator.isEmpty()) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The shortcut format is invalid.");
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut failed reason=invalid-accelerator shortcut=%1").arg(normalizedShortcut));
        return false;
    }

    QString stderrText;
    const bool ok = runProcess(gsettingsPath(),
                               {
                                   QStringLiteral("--schemadir"),
                                   schemaDir(),
                                   QStringLiteral("set"),
                                   QString::fromLatin1(kShortcutSchema),
                                   QString::fromLatin1(kShortcutKey),
                                   QStringLiteral("['%1']").arg(accelerator)
                               },
                               nullptr,
                               &stderrText);
    if (!ok && errorMessage) {
        *errorMessage = stderrText.isEmpty()
            ? QStringLiteral("The shortcut could not be updated.")
            : stderrText;
    }
    if (ok) {
        refreshGnomeExtensionShortcutBinding();
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut success accelerator=%1").arg(accelerator));
    } else {
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-configured-shortcut failed accelerator=%1 error=%2").arg(accelerator).arg(stderrText));
    }
    return ok;
}

bool AppIntegration::isAutostartEnabled() {
    return QFileInfo::exists(autostartFilePath());
}

bool AppIntegration::setAutostartEnabled(bool enabled, QString *errorMessage) {
    const QString path = autostartFilePath();
    RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart begin enabled=%1 path=%2").arg(enabled ? QStringLiteral("true") : QStringLiteral("false")).arg(path));
    if (enabled) {
        QDir dir(QFileInfo(path).absolutePath());
        if (!dir.mkpath(QStringLiteral("."))) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The autostart directory could not be created.");
            }
            RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart failed reason=mkdir"));
            return false;
        }

        QSaveFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The autostart file could not be written.");
            }
            RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart failed reason=open-write"));
            return false;
        }

        QTextStream stream(&file);
        stream << autostartDesktopEntry();
        if (!file.commit()) {
            if (errorMessage) {
                *errorMessage = QStringLiteral("The autostart file could not be saved.");
            }
            RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart failed reason=commit"));
            return false;
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart success enabled=true"));
        return true;
    }

    if (QFile::exists(path) && !QFile::remove(path)) {
        if (errorMessage) {
            *errorMessage = QStringLiteral("The autostart file could not be removed.");
        }
        RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart failed reason=remove"));
        return false;
    }
    RuntimeLog::write(QStringLiteral("AppIntegration"), QStringLiteral("set-autostart success enabled=false"));
    return true;
}
