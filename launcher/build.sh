#!/usr/bin/env bash
set -euo pipefail

LAUNCHER_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT_DIR="$(cd "$LAUNCHER_DIR/.." && pwd)"

if [[ "$#" -gt 0 && "$1" == "clean" ]]; then
    make -C "$LAUNCHER_DIR" clean
    exit 0
fi

SDK_DIR="${SDK_DIR:-$ROOT_DIR/sdk}"
UI_DIR="${UI_DIR:-$ROOT_DIR/ui}"
SDK_DIR="$(cd "$SDK_DIR" && pwd)"
UI_DIR="$(cd "$UI_DIR" && pwd)"

echo "Hollyhock Launcher Build"
echo "Launcher: $LAUNCHER_DIR"
echo "SDK:      $SDK_DIR"
echo "UI:       $UI_DIR"
echo
echo "==> Building SDK..."
make -C "$SDK_DIR"
echo "==> Building UI library..."
make -C "$UI_DIR" SDK_DIR="$SDK_DIR"
echo "==> Building launcher..."
if [[ "$#" -eq 0 ]]; then
    make -C "$LAUNCHER_DIR" SDK_DIR="$SDK_DIR" UI_DIR="$UI_DIR" all
else
    make -C "$LAUNCHER_DIR" SDK_DIR="$SDK_DIR" UI_DIR="$UI_DIR" "$@"
fi

echo
echo "Build completed: $LAUNCHER_DIR/run.bin"
