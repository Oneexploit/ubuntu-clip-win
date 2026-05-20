#!/usr/bin/env bash
set -euo pipefail

sudo apt update
sudo apt install -y \
  build-essential \
  cmake \
  libx11-dev \
  libxcb1-dev \
  qt6-base-dev \
  libqt6sql6-sqlite \
  qt6-qpa-plugins \
  xdotool \
  libglib2.0-bin \
  gnome-shell-extension-prefs
