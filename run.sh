#!/usr/bin/env bash
set -e

MODE="${1:-debug}"

if [[ "$MODE" != "debug" && "$MODE" != "release" ]]; then
    echo "Usage: $0 [debug|release]"
    exit 1
fi

cmake --preset "$MODE"
cmake --build --preset "$MODE"

ln -sf build/debug/compile_commands.json compile_commands.json

ctest --test-dir "build/$MODE"
