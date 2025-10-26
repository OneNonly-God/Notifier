#!/usr/bin/env bash
set -e  # exit on error

# Project root (where this script lives)
ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$ROOT_DIR/build"

# Create build directory if not exists
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

echo ">>> Configuring project with CMake..."
cmake ..

echo ">>> Building project..."
make -j$(nproc)

echo ">>> Build complete! Running the application..."
./notifier