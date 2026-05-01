#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$ROOT_DIR/build"

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build "$BUILD_DIR" -j"$(nproc)"
sudo cmake --install "$BUILD_DIR"

mkdir -p "$HOME/.config/autostart"
cp "$ROOT_DIR/packaging/ubuntu-clip-win-autostart.desktop" "$HOME/.config/autostart/ubuntu-clip-win.desktop"

if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  sudo gtk-update-icon-cache -q -t -f /usr/local/share/icons/hicolor >/dev/null 2>&1 || true
fi

if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database "$HOME/.local/share/applications" >/dev/null 2>&1 || true
fi

if [[ -n "${XDG_CURRENT_DESKTOP:-}" ]] && [[ "${XDG_CURRENT_DESKTOP,,}" == *gnome* ]]; then
  "$ROOT_DIR/gnome-extension/install.sh" || true
elif command -v gnome-extensions >/dev/null 2>&1; then
  "$ROOT_DIR/gnome-extension/install.sh" || true
fi

pkill ubuntu-clip-win >/dev/null 2>&1 || true
nohup ubuntu-clip-win --background >/dev/null 2>&1 &

echo "Installed and started in background. Run: ubuntu-clip-win --show"
echo "Shortcut: Ctrl+Super+V"
echo "History is text-only and session-only; it is cleared automatically after logout/reboot."
