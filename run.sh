#!/bin/sh
set -eu

cd -- "$(dirname -- "$0")"

if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build --config Release --target empty_gui --parallel
exec ./build/empty_gui.app/Contents/MacOS/empty_gui
