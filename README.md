![Clipboard for Linux](assets/ubuntu-clip-win-512.png)

# Clipboard for Linux

A modern, lightweight, Windows-inspired clipboard history popup for Ubuntu/Linux.

This project brings a familiar clipboard history experience to Ubuntu: press **Ctrl + Super + V**, browse your copied text history, select an item with the keyboard or mouse, and paste it quickly.

The app is designed to be simple, fast, and user-friendly. It focuses on **text-only clipboard history** to avoid heavy memory usage and privacy issues caused by storing files or images.

---

## Features

- Modern clipboard history popup for Ubuntu/Linux
- Opens with **Ctrl + Super + V**
- Text-only history: text, code snippets, tokens, terminal output, logs, commands, URLs, and plain text
- Keyboard-friendly navigation
  - `Up` / `Down` to move between items
  - `Enter` to paste the selected item
  - `Delete` to remove an item
  - `Ctrl + Delete` to clear history
- Newest copied items appear at the top
- Older items stay lower in the list
- Draggable popup window
- Search support
- Lightweight background process
- Session-only history
  - Clipboard history is automatically cleared after logout, shutdown, or reboot
- Does not store files, images, PNGs, SVG files, or binary clipboard data
- Tray icon support
- GNOME Shell Extension for the global shortcut
- Built with C++ and Qt 6

---

## What This App Stores

This clipboard manager stores only text-based clipboard content, such as:

- Plain text
- Source code
- Terminal output
- Commands
- Logs
- Tokens copied as text
- URLs copied as text
- SVG/XML content copied from an editor as text

It does **not** store:

- Copied files
- Copied folders
- Images
- PNG/JPG/SVG files copied from the file manager
- Binary clipboard data

This keeps the app lightweight and prevents the clipboard database from becoming too large.

---

## Shortcut

Default shortcut:

```text
Ctrl + Super + V
```

On most Linux keyboards, the **Super** key is the same as the **Windows** key.

So the shortcut is:

```text
Ctrl + Win + V
```

---

## Requirements

Ubuntu with Qt 6 development packages installed.

Install dependencies:

```bash
sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  qt6-base-dev \
  libqt6sql6-sqlite \
  qt6-qpa-plugins \
  xdotool \
  libglib2.0-bin \
  gnome-shell-extension-prefs
```

---

## Install

Clone the repository:

```bash
git clone https://github.com/YOUR_USERNAME/YOUR_REPOSITORY.git
cd YOUR_REPOSITORY
```

Build and install:

```bash
./packaging/install-user.sh
```

The installer will:

- Build the app with CMake
- Install the binary to `/usr/local/bin/ubuntu-clip-win`
- Install the desktop entry
- Install the app icon
- Install the autostart entry
- Install the GNOME Shell Extension shortcut
- Start the app in the background

---

## Run

Show the clipboard popup:

```bash
ubuntu-clip-win --show
```

Run in the background:

```bash
ubuntu-clip-win --background
```

Quit the app:

```bash
pkill ubuntu-clip-win
```

---

## GNOME Shortcut Setup

The installer tries to install the GNOME Shell Extension automatically.

Manual installation:

```bash
./gnome-extension/install.sh
```

If the shortcut does not work immediately on Wayland, log out and log back in.

Default shortcut:

```text
Ctrl + Super + V
```

---

## Usage

1. Copy any text, code, terminal output, command, or token.
2. Press **Ctrl + Super + V**.
3. Use `Up` / `Down` to select an item.
4. Press `Enter` to paste it.

You can also click an item with the mouse.

---

## Memory and Privacy Design

This project intentionally uses a **session-only** clipboard history.

That means the history is cleared automatically when the session ends, such as after:

- Logout
- Reboot
- Shutdown
- App restart

This keeps sensitive copied content from staying on disk permanently.

The app also avoids storing files and images, which helps keep memory usage predictable and prevents accidental storage of large clipboard items.

---

## Build from Source

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=/usr/local
cmake --build build -j"$(nproc)"
sudo cmake --install build
```

Then run:

```bash
ubuntu-clip-win --show
```

---

## Project Structure

```text
.
├── assets/                 # App icons and Qt resources
├── gnome-extension/        # GNOME Shell Extension for Ctrl + Super + V
├── packaging/              # Install, uninstall, and packaging scripts
├── src/                    # C++ / Qt source code
├── CMakeLists.txt
└── README.md
```

---

## Tech Stack

- C++17
- Qt 6
- Qt Widgets
- SQLite in-memory/session storage
- CMake
- GNOME Shell Extension
- xdotool for X11 paste integration

---

## Notes About Wayland and X11

Linux desktop environments handle global shortcuts and paste automation differently.

- On GNOME/Wayland, the GNOME Shell Extension is used for the global shortcut.
- On X11, paste automation can be handled more directly with `xdotool`.
- On Wayland, some apps or desktop environments may restrict simulated keyboard input for security reasons.

The selected text is always placed into the system clipboard before paste is attempted.

---

## Uninstall

```bash
./packaging/uninstall-user.sh
```

Or manually:

```bash
pkill ubuntu-clip-win || true
sudo rm -f /usr/local/bin/ubuntu-clip-win
sudo rm -f /usr/local/share/applications/ubuntu-clip-win.desktop
sudo rm -f /etc/xdg/autostart/ubuntu-clip-win-autostart.desktop
rm -rf ~/.local/share/gnome-shell/extensions/ubuntu-clip-win@amirhosein.local
```

---

## Roadmap

Planned improvements:

- Better Wayland paste integration where supported
- More polished animations
- Settings page
- Custom shortcut configuration
- Optional persistent history mode
- Import/export settings
- More desktop environment support

---

## License

This project is released under the MIT License.

---

## Author

Built by AmirHosein.
