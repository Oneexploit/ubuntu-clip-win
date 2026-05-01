#!/usr/bin/env bash
set -euo pipefail

sudo rm -f /usr/local/bin/ubuntu-clip-win
sudo rm -f /usr/local/share/applications/ubuntu-clip-win.desktop
sudo rm -f /usr/local/share/icons/hicolor/scalable/apps/ubuntu-clip-win.svg
sudo rm -f /usr/local/share/icons/hicolor/64x64/apps/ubuntu-clip-win.png
sudo rm -f /usr/local/share/icons/hicolor/128x128/apps/ubuntu-clip-win.png
sudo rm -f /usr/local/share/icons/hicolor/256x256/apps/ubuntu-clip-win.png
sudo rm -f /usr/local/share/icons/hicolor/512x512/apps/ubuntu-clip-win.png
rm -f "$HOME/.config/autostart/ubuntu-clip-win.desktop"
rm -rf "$HOME/.local/share/gnome-shell/extensions/ubuntu-clip-win@amirhosein.local"

echo "Uninstalled user-level files. Session clipboard history is stored in the runtime directory and is cleared after logout/reboot."
