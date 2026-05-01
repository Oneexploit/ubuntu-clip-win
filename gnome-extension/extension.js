import Gio from 'gi://Gio';
import Meta from 'gi://Meta';
import Shell from 'gi://Shell';

import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';

const KEYBINDING = 'show-ubuntu-clip-win';
const COMMANDS = [
    '/usr/local/bin/ubuntu-clip-win',
    '/usr/bin/ubuntu-clip-win',
    'ubuntu-clip-win',
];

export default class UbuntuClipWinHotkeyExtension extends Extension {
    enable() {
        this._settings = this.getSettings();
        Main.wm.addKeybinding(
            KEYBINDING,
            this._settings,
            Meta.KeyBindingFlags.NONE,
            Shell.ActionMode.NORMAL | Shell.ActionMode.OVERVIEW,
            () => this._openClipboardPopup()
        );
    }

    disable() {
        Main.wm.removeKeybinding(KEYBINDING);
        this._settings = null;
    }

    _openClipboardPopup() {
        let lastError = null;
        for (const command of COMMANDS) {
            try {
                Gio.Subprocess.new(
                    [command, '--show'],
                    Gio.SubprocessFlags.NONE
                );
                return;
            } catch (error) {
                lastError = error;
            }
        }
        if (lastError) {
            logError(lastError, 'Ubuntu Clip Win: could not run ubuntu-clip-win --show');
        }
    }
}
