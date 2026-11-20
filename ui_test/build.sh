#!/usr/bin/env bash
set -e

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SDK_DIR="${SDK_DIR:-"$APP_DIR/../sdk"}"
UI_DIR="${UI_DIR:-"$APP_DIR/../ui"}"
SDK_DIR="$(cd "$SDK_DIR" && pwd)"
UI_DIR="$(cd "$UI_DIR" && pwd)"

echo "========================================"
echo " Hollyhock 2.1 Build"
echo "========================================"
echo
echo "App directory: $APP_DIR"
echo "SDK directory: $SDK_DIR"
echo "UI directory:  $UI_DIR"
echo

echo "==> Clean all build artifacts..."
make -C "$APP_DIR" SDK_DIR="$SDK_DIR" UI_DIR="$UI_DIR" clean
make -C "$UI_DIR" SDK_DIR="$SDK_DIR" clean
make -C "$SDK_DIR" clean
echo

echo "==> Building SDK..."
make -C "$SDK_DIR"
echo

echo "==> Building UI library..."
make -C "$UI_DIR" SDK_DIR="$SDK_DIR"
echo

echo "==> Building application..."
if [ "$#" -eq 0 ]; then
    make -C "$APP_DIR" SDK_DIR="$SDK_DIR" UI_DIR="$UI_DIR" all
else
    make -C "$APP_DIR" SDK_DIR="$SDK_DIR" UI_DIR="$UI_DIR" "$@"
fi

echo
echo "========================================"
echo " Build completed successfully!"
echo "========================================"
