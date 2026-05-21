#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
PREFIX="/usr/local"
BUILD_DIR="$ROOT_DIR/build"
INSTALL_DEPS=1
START_AFTER_INSTALL=1
TARGET_USER="${SUDO_USER:-$USER}"
TARGET_HOME="${HOME}"
SESSION_TYPE="${XDG_SESSION_TYPE:-unknown}"
DESKTOP_ENV="${XDG_CURRENT_DESKTOP:-unknown}"

if [[ -n "${SUDO_USER:-}" ]]; then
  TARGET_HOME="$(getent passwd "$SUDO_USER" | cut -d: -f6)"
fi

log_step() {
  echo "[install] $*"
}

warn_step() {
  echo "[install] warning: $*" >&2
}

have_command() {
  command -v "$1" >/dev/null 2>&1
}

maybe_install_missing_runtime_packages() {
  local session_lc="${SESSION_TYPE,,}"
  local desktop_lc="${DESKTOP_ENV,,}"
  local -a packages=()

  if [[ "$session_lc" == "wayland" ]] && ! have_command wl-paste; then
    packages+=(wl-clipboard)
  fi

  if [[ "$session_lc" == "x11" ]] && ! have_command xdotool; then
    packages+=(xdotool)
  fi

  if [[ "$desktop_lc" == *gnome* ]] && ! have_command gsettings; then
    packages+=(libglib2.0-bin)
  fi

  if [[ ${#packages[@]} -eq 0 ]]; then
    return 0
  fi

  if [[ $INSTALL_DEPS -ne 1 ]]; then
    warn_step "Missing runtime packages detected: ${packages[*]}. Re-run install without --skip-deps to install them automatically."
    return 0
  fi

  if ! have_command apt-get; then
    warn_step "Missing runtime packages detected (${packages[*]}), but apt-get is not available in this environment."
    return 0
  fi

  log_step "Installing missing runtime packages: ${packages[*]}"
  sudo apt-get update
  sudo apt-get install -y "${packages[@]}"
}

print_runtime_summary() {
  local extension_dir="$TARGET_HOME/.local/share/gnome-shell/extensions/ubuntu-clip-win@amirhosein.local"
  local running_process
  running_process="$(pgrep -a -f "$BINARY_PATH" 2>/dev/null | head -n 1 || true)"

  echo "Runtime checks:"
  echo "  Session: $SESSION_TYPE"
  echo "  Desktop: $DESKTOP_ENV"
  echo "  wl-paste: $(command -v wl-paste 2>/dev/null || echo missing)"
  echo "  xdotool: $(command -v xdotool 2>/dev/null || echo missing)"
  echo "  gsettings: $(command -v gsettings 2>/dev/null || echo missing)"
  echo "  gnome-extensions: $(command -v gnome-extensions 2>/dev/null || echo missing)"
  echo "  GNOME extension dir: $([[ -d "$extension_dir" ]] && echo "$extension_dir" || echo missing)"
  echo "  Autostart file: $AUTOSTART_FILE"
  echo "  Running process: ${running_process:-not detected}"

  if [[ "${SESSION_TYPE,,}" == "wayland" ]] && ! have_command wl-paste; then
    warn_step "wl-paste is missing. Rapid clipboard fallback on Wayland will stay unreliable until wl-clipboard is installed."
  fi

  if [[ "${SESSION_TYPE,,}" == "x11" ]] && ! have_command xdotool; then
    warn_step "xdotool is missing. One-key auto-paste on X11 will be unavailable."
  fi

  if [[ "${SESSION_TYPE,,}" == "wayland" ]] && [[ "${DESKTOP_ENV,,}" == *gnome* ]] && [[ ! -d "$extension_dir" ]]; then
    warn_step "GNOME Wayland shortcut extension files were not found. Global shortcut support may be unavailable until the extension is installed."
  fi
}

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
  log_step "Installing build and runtime dependencies"
  "$ROOT_DIR/packaging/install-deps-ubuntu.sh"
fi

maybe_install_missing_runtime_packages

log_step "Configuring CMake"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DBUILD_TESTING=OFF
log_step "Building application"
cmake --build "$BUILD_DIR" -j"$(nproc)"
log_step "Installing application"
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
  sleep 1
fi

echo "Install completed."
echo "Binary: $BINARY_PATH"
echo "Open popup: $BINARY_PATH --show"
echo "Open settings: $BINARY_PATH --settings"
echo "Shortcut: Ctrl+Alt+V"
if [[ $START_AFTER_INSTALL -eq 1 ]]; then
  echo "The app was started in the background."
fi
print_runtime_summary
