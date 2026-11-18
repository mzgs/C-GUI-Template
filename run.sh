#!/bin/sh
set -eu

cd -- "$(dirname -- "$0")"

if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
cmake --build build --config Release --target empty_gui --parallel
exec "./build/C GUI Template.app/Contents/MacOS/C GUI Template"
