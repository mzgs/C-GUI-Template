#!/bin/sh
set -eu

cd -- "$(dirname -- "$0")"

if [ ! -f build/CMakeCache.txt ]; then
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
fi
exec cmake --build build --config Release --target run --parallel
