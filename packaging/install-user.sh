#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="/usr/local"
BUILD_DIR="$ROOT_DIR/build"
INSTALL_DEPS=1
START_AFTER_INSTALL=1
TARGET_USER="${SUDO_USER:-$USER}"
TARGET_HOME="${HOME}"

if [[ -n "${SUDO_USER:-}" ]]; then
  TARGET_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
fi

usage() {
  cat <<'EOF'
Usage: bash packaging/install-user.sh [options]

Options:
  --skip-deps         Skip Ubuntu dependency installation
  --no-start          Do not start the app after install
  --prefix PATH       Install prefix (default: /usr/local)
  --build-dir PATH    Custom build directory
  --help              Show this help
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --skip-deps)
      INSTALL_DEPS=0
      shift
      ;;
    --no-start)
      START_AFTER_INSTALL=0
      shift
      ;;
    --prefix)
      PREFIX="${2:-}"
      shift 2
      ;;
    --build-dir)
      BUILD_DIR="${2:-}"
      shift 2
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

if [[ -z "$PREFIX" || -z "$BUILD_DIR" ]]; then
  echo "Install prefix and build directory must not be empty." >&2
  exit 1
fi

if [[ -z "$TARGET_USER" || -z "$TARGET_HOME" ]]; then
  echo "Could not determine the target user for user-level install steps." >&2
  exit 1
fi

if [[ $INSTALL_DEPS -eq 1 && -x "$ROOT_DIR/packaging/install-deps-ubuntu.sh" && -x "$(command -v apt-get || true)" ]]; then
  "$ROOT_DIR/packaging/install-deps-ubuntu.sh"
fi

cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTING=OFF
cmake --build "$BUILD_DIR" -j"$(nproc)"
sudo cmake --install "$BUILD_DIR"

BINARY_PATH="$PREFIX/bin/ubuntu-clip-win"
APPLICATIONS_DIR="$PREFIX/share/applications"
ICONS_DIR="$PREFIX/share/icons/hicolor"
AUTOSTART_FILE="$TARGET_HOME/.config/autostart/ubuntu-clip-win.desktop"

if [[ ! -x "$BINARY_PATH" ]]; then
  echo "Installed binary not found at: $BINARY_PATH" >&2
  exit 1
fi

mkdir -p "$(dirname "$AUTOSTART_FILE")"
cat > "$AUTOSTART_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=Clipboard History
Comment=Start clipboard history in the background
Exec="$BINARY_PATH" --background
Icon=ubuntu-clip-win
Terminal=false
X-GNOME-Autostart-enabled=true
EOF
chown "$TARGET_USER":"$TARGET_USER" "$AUTOSTART_FILE" >/dev/null 2>&1 || true

if command -v gtk-update-icon-cache >/dev/null 2>&1 && [[ -d "$ICONS_DIR" ]]; then
  sudo gtk-update-icon-cache -q -t -f "$ICONS_DIR" >/dev/null 2>&1 || true
fi

if command -v update-desktop-database >/dev/null 2>&1 && [[ -d "$APPLICATIONS_DIR" ]]; then
  sudo update-desktop-database "$APPLICATIONS_DIR" >/dev/null 2>&1 || true
fi

if [[ -n "${XDG_CURRENT_DESKTOP:-}" ]] && [[ "${XDG_CURRENT_DESKTOP,,}" == *gnome* ]]; then
  if [[ -n "${SUDO_USER:-}" ]]; then
    sudo -u "$TARGET_USER" env HOME="$TARGET_HOME" bash "$ROOT_DIR/gnome-extension/install.sh" || true
  else
    bash "$ROOT_DIR/gnome-extension/install.sh" || true
  fi
elif command -v gnome-extensions >/dev/null 2>&1; then
  if [[ -n "${SUDO_USER:-}" ]]; then
    sudo -u "$TARGET_USER" env HOME="$TARGET_HOME" bash "$ROOT_DIR/gnome-extension/install.sh" || true
  else
    bash "$ROOT_DIR/gnome-extension/install.sh" || true
  fi
fi

pkill ubuntu-clip-win >/dev/null 2>&1 || true

if [[ $START_AFTER_INSTALL -eq 1 ]]; then
  if [[ -n "${SUDO_USER:-}" ]]; then
    sudo -u "$TARGET_USER" env HOME="$TARGET_HOME" nohup "$BINARY_PATH" --background >/dev/null 2>&1 &
  else
    nohup "$BINARY_PATH" --background >/dev/null 2>&1 &
  fi
fi

echo "Install completed."
echo "Binary: $BINARY_PATH"
echo "Open popup: $BINARY_PATH --show"
echo "Open settings: $BINARY_PATH --settings"
echo "Shortcut: Ctrl+Alt+V"
if [[ $START_AFTER_INSTALL -eq 1 ]]; then
  echo "The app was started in the background."
fi
