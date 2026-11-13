#!/usr/bin/env bash
# Builds Diorama. On Windows (Git Bash) this runs build.bat; on Linux: cmake, a C++17 compiler and libsdl2-dev.
set -e
cd "$(dirname "$0")"
case "$(uname -s)" in MINGW*|MSYS*|CYGWIN*) cmd //c build.bat "$@"; exit $? ;; esac
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
echo "Built: build/Diorama"
