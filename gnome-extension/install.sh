#!/usr/bin/env bash
set -euo pipefail

UUID="ubuntu-clip-win@amirhosein.local"
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
DEST="$HOME/.local/share/gnome-shell/extensions/$UUID"
SHORTCUT="['<Control><Alt>v']"
SCHEMA="org.gnome.shell.extensions.ubuntu-clip-win"
KEY="show-ubuntu-clip-win"

mkdir -p "$DEST/schemas"
cp "$ROOT_DIR/metadata.json" "$DEST/metadata.json"
cp "$ROOT_DIR/extension.js" "$DEST/extension.js"
cp "$ROOT_DIR/schemas/org.gnome.shell.extensions.ubuntu-clip-win.gschema.xml" "$DEST/schemas/"

glib-compile-schemas "$DEST/schemas"

if command -v gsettings >/dev/null 2>&1; then
  gsettings --schemadir "$DEST/schemas" set "$SCHEMA" "$KEY" "$SHORTCUT" || true
fi

if command -v gnome-extensions >/dev/null 2>&1; then
  gnome-extensions disable "$UUID" >/dev/null 2>&1 || true
  gnome-extensions enable "$UUID" >/dev/null 2>&1 || true
fi

echo "GNOME extension installed: $UUID"
echo "Shortcut: Ctrl+Alt+V"
echo "On Wayland, log out and log back in if the shortcut does not appear immediately."
