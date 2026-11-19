#!/usr/bin/env bash

set -e

# Resolve paths relative to this script
APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# Use SDK_DIR from environment or default to ../sdk
SDK_DIR="${SDK_DIR:-"$APP_DIR/../sdk"}"
SDK_DIR="$(cd "$SDK_DIR" && pwd)"

echo "========================================"
echo " Hollyhock 2.1 Build"
echo "========================================"
echo
echo "App directory: $APP_DIR"
echo "SDK directory: $SDK_DIR"
echo

# Clean Build
echo "==> Clean Artifacts..."
make -C "$APP_DIR" SDK_DIR="$SDK_DIR" clean
echo

# Build SDK
echo "==> Building SDK..."
make -C "$SDK_DIR"

echo
echo "==> Building application..."

# Default target: build both .hhk and .bin
if [ "$#" -eq 0 ]; then
    make -C "$APP_DIR" SDK_DIR="$SDK_DIR" all
else
    make -C "$APP_DIR" SDK_DIR="$SDK_DIR" "$@"
fi

echo
echo "========================================"
echo " Build completed successfully!"
echo "========================================"
