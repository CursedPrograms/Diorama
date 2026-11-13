#!/usr/bin/env bash
# Installs Diorama. On Windows (Git Bash) this runs install.bat; on Linux it copies the build to ~/.local/share/diorama.
set -e
cd "$(dirname "$0")"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) cmd //c install.bat "$@"; exit $? ;; esac
[ -f build/Diorama ] || { echo "Build it first: ./build.sh"; exit 1; }
mkdir -p ~/.local/share/diorama ~/.local/bin
cp -r build/Diorama build/assets build/templates ~/.local/share/diorama/
ln -sf ~/.local/share/diorama/Diorama ~/.local/bin/diorama
echo "Installed: ~/.local/bin/diorama"
