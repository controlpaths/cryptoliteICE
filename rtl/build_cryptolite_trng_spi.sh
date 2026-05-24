#!/usr/bin/env bash
# build_cryptolite_trng_spi.sh — synthesis, P&R and packing of the TRNG SPI top
# Usage: bash rtl/build_cryptolite_trng_spi.sh   [from anywhere]
#
# Toolchain: the open-source IceStorm flow (yosys, nextpnr-ice40, icepack).
#            They must be on PATH, or set ICE40_TOOLCHAIN to the directory
#            holding them.
# Device: iCE40UP5K   Package: SG48
#
# controlpaths.com | lut7.dev

set -e

RTL_DIR="$(cd "$(dirname "$0")" && pwd)"          # rtl/ — holds the sources + .pcf
REPO_ROOT="$(cd "$RTL_DIR/.." && pwd)"
BUILD="$REPO_ROOT/build/cryptolite_trng_spi"

RTL_TOP="$RTL_DIR/cryptolite_trng_spi.v"
RTL_SLAVE="$RTL_DIR/spi_trng_slave.v"
RTL_RO="$RTL_DIR/ro_trng.v"
RTL_CELL="$RTL_DIR/ro_cell.v"
PCF="$RTL_DIR/cryptolite_trng_spi.pcf"

# Optionally prepend a local toolchain directory to PATH.
if [ -n "${ICE40_TOOLCHAIN:-}" ]; then
    export PATH="$ICE40_TOOLCHAIN:$PATH"
fi

echo "==> Checking toolchain..."
for tool in yosys nextpnr-ice40 icepack; do
    command -v "$tool" >/dev/null 2>&1 || { echo "ERROR: $tool not found on PATH (set ICE40_TOOLCHAIN?)"; exit 1; }
done

mkdir -p "$BUILD"

echo "==> Synthesis with yosys..."
yosys -p "synth_ice40 -top cryptolite_trng_spi -json $BUILD/cryptolite_trng_spi.json" \
    "$RTL_TOP" "$RTL_SLAVE" "$RTL_RO" "$RTL_CELL"

echo "==> Place & Route with nextpnr-ice40..."
nextpnr-ice40 \
    --up5k \
    --package sg48 \
    --json    "$BUILD/cryptolite_trng_spi.json" \
    --asc     "$BUILD/cryptolite_trng_spi.asc" \
    --pcf     "$PCF" \
    --ignore-loops \
    --timing-allow-fail

echo "==> Packing with icepack..."
icepack "$BUILD/cryptolite_trng_spi.asc" "$BUILD/cryptolite_trng_spi.bin"

echo ""
echo "==> Build succeeded."
echo "    Bitstream: $BUILD/cryptolite_trng_spi.bin"
echo ""
echo "To program the iCE40 from the host:"
echo "    host/criptolite-ice.py update-fpga $BUILD/cryptolite_trng_spi.bin"
