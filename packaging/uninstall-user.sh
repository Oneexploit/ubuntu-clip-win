#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="/usr/local"
KEEP_DATA=0

usage() {
  cat <<'EOF'
Usage: bash packaging/uninstall-user.sh [options]

Options:
  --prefix PATH       Remove files from this install prefix (default: /usr/local)
  --keep-data         Keep user settings and clipboard database
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --prefix)
      PREFIX="${2:-}"
      shift 2
      ;;
    --keep-data)
      KEEP_DATA=1
      shift
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if [[ -z "$PREFIX" ]]; then
  echo "Install prefix must not be empty." >&2
  exit 1
fi

APPLICATIONS_DIR="$PREFIX/share/applications"
ICONS_DIR="$PREFIX/share/icons/hicolor"
AUTOSTART_FILE="$HOME/.config/autostart/ubuntu-clip-win.desktop"
LEGACY_SYSTEM_AUTOSTART="/etc/xdg/autostart/ubuntu-clip-win-autostart.desktop"

pkill ubuntu-clip-win >/dev/null 2>&1 || true

if [[ -x "$ROOT_DIR/gnome-extension/uninstall.sh" ]]; then
  "$ROOT_DIR/gnome-extension/uninstall.sh" || true
else
  rm -rf "$HOME/.local/share/gnome-shell/extensions/ubuntu-clip-win@amirhosein.local"
fi

sudo rm -f "$PREFIX/bin/ubuntu-clip-win"
sudo rm -f "$APPLICATIONS_DIR/ubuntu-clip-win.desktop"
sudo rm -f "$ICONS_DIR/scalable/apps/ubuntu-clip-win.svg"
sudo rm -f "$ICONS_DIR/64x64/apps/ubuntu-clip-win.png"
sudo rm -f "$ICONS_DIR/128x128/apps/ubuntu-clip-win.png"
sudo rm -f "$ICONS_DIR/256x256/apps/ubuntu-clip-win.png"
sudo rm -f "$ICONS_DIR/512x512/apps/ubuntu-clip-win.png"
sudo rm -f "$LEGACY_SYSTEM_AUTOSTART" || true
rm -f "$AUTOSTART_FILE"

if [[ $KEEP_DATA -eq 0 ]]; then
  rm -rf "$HOME/.local/share/AmirHosein/Ubuntu Clip Win"
  rm -rf "$HOME/.local/share/ubuntu-clip-win"
  rm -f "$HOME/.config/AmirHosein/Ubuntu Clip Win.conf"
  rm -f "$HOME/.config/AmirHosein/Ubuntu Clip Win.ini"
fi

if command -v gtk-update-icon-cache >/dev/null 2>&1 && [[ -d "$ICONS_DIR" ]]; then
  sudo gtk-update-icon-cache -q -t -f "$ICONS_DIR" >/dev/null 2>&1 || true
fi

if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$APPLICATIONS_DIR" ]]; then
  sudo update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi

echo "Uninstall completed."
if [[ $KEEP_DATA -eq 1 ]]; then
  echo "User settings and clipboard database were kept."
else
  echo "User settings and clipboard database were removed."
fi
