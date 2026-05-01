#!/usr/bin/env bash
set -euo pipefail

UUID="ubuntu-clip-win@amirhosein.local"

if command -v gnome-extensions >/dev/null 2>&1; then
  gnome-extensions disable "$UUID" || true
fi
rm -rf "$HOME/.local/share/gnome-shell/extensions/$UUID"

echo "GNOME extension removed: $UUID"
