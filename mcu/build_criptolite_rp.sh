#!/usr/bin/env bash
# build_criptolite_rp.sh — build the Vendor firmware (criptolite_rp); the .uf2 lands in build/
# Usage: bash mcu/build_criptolite_rp.sh   [from anywhere]
#
# Requires: arm-none-eabi-gcc, CMake >= 3.13, make and a Pico SDK checkout.
#           Point PICO_SDK_PATH at it, or place it as ./pico-sdk at the repo root.
#
# controlpaths.com | lut7.dev

set -e

APP_DIR="$(cd "$(dirname "$0")" && pwd)"          # mcu/ — C sources + CMakeLists.txt
REPO_ROOT="$(cd "$APP_DIR/.." && pwd)"
BUILD_DIR="$REPO_ROOT/build/criptolite_rp"
PICO_SDK="${PICO_SDK_PATH:-$REPO_ROOT/pico-sdk}"

if [ ! -f "$PICO_SDK/external/pico_sdk_import.cmake" ]; then
    echo "ERROR: Pico SDK not found at '$PICO_SDK'."
    echo "       Set PICO_SDK_PATH or clone it to '$REPO_ROOT/pico-sdk'."
    exit 1
fi

# Make sure the TinyUSB submodule is initialised inside the SDK.
TINYUSB_DIR="$PICO_SDK/lib/tinyusb"
if [ ! -f "$TINYUSB_DIR/src/tusb.h" ]; then
    echo "==> Initialising the TinyUSB submodule..."
    git -C "$PICO_SDK" submodule update --init lib/tinyusb
fi

echo "==> Build directory: $BUILD_DIR"
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

PICO_SDK_PATH="$PICO_SDK" cmake "$APP_DIR" -DCMAKE_BUILD_TYPE=Release

make -j"$(nproc)"

echo ""
echo "==> Build succeeded."
echo "    Firmware UF2: $BUILD_DIR/criptolite_rp.uf2"
echo ""
echo "To flash the RP2040 for the first time:"
echo "  1. Hold SW1 (BOOTSEL) and connect USB."
echo "  2. The RP2040 mounts as the 'RPI-RP2' drive."
echo "  3. Copy the .uf2:"
echo "       cp $BUILD_DIR/criptolite_rp.uf2 /media/\$USER/RPI-RP2/"
echo ""
echo "Afterwards you can update it over USB with:"
echo "    host/criptolite-ice.py update-rp2040 $BUILD_DIR/criptolite_rp.uf2"
