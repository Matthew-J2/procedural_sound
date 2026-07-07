#!/usr/bin/env bash
set -e

PRESET="${1:-debug}"

cmake --preset "$PRESET"
cmake --build --preset "$PRESET"

ln -sf build/$PRESET/compile_commands.json compile_commands.json

ctest --test-dir "build/$PRESET"
