#!/usr/bin/env python3
"""
criptolite-ice.py - Host-side client for the CryptoLite-RP firmware.

The firmware exposes a single USB Vendor interface with two bulk endpoints
and the binary protocol described in mcu/protocol.h. This
script wraps that protocol into a small CLI:

    criptolite-ice.py status
    criptolite-ice.py random -n 32                       # 32 bytes, hex
    criptolite-ice.py random -n 1048576 -o rng.bin       # 1 MiB to file
    criptolite-ice.py random --bits 1000000 -o rng.bin   # N bits -> ceil(N/8) bytes
    criptolite-ice.py update-fpga bitstream.bin          # asks for confirmation
    criptolite-ice.py update-rp2040 firmware.uf2         # asks for confirmation

Requires pyusb. On Linux you may need a udev rule to grant access without
sudo:

    SUBSYSTEM=="usb", ATTRS{idVendor}=="2e8a", ATTRS{idProduct}=="000f", \
        MODE="0666", TAG+="uaccess"

controlpaths.com | lut7.dev
"""

import argparse
import math
import os
import shutil
import struct
import sys
import time
from pathlib import Path

import usb.core
import usb.util


VID = 0x2E8A
PID = 0x000F

EP_OUT = 0x01
EP_IN  = 0x81
EP_IN_MAX_PKT = 64  # full-speed bulk endpoint

# ── Protocol constants (mirror of protocol.h) ───────────────────────────
CMD_STATUS              = 0x01
CMD_GET_RANDOM          = 0x02
CMD_FLASH_BEGIN         = 0x03
CMD_FLASH_DATA          = 0x04
CMD_FLASH_END           = 0x05
CMD_REBOOT_BOOTLOADER   = 0x06

PROTO_OK                = 0x00
PROTO_ERR_BAD_CMD       = 0x01
PROTO_ERR_BAD_LENGTH    = 0x02
PROTO_ERR_BUSY          = 0x03
PROTO_ERR_FLASH         = 0x04
PROTO_ERR_HEALTH_FAIL   = 0x05
PROTO_ERR_HEALTH_WARM   = 0x06
PROTO_ERR_NO_ENTROPY    = 0x07
PROTO_ERR_INTERNAL      = 0xFF

ERROR_NAMES = {
    PROTO_OK:                "OK",
    PROTO_ERR_BAD_CMD:       "BAD_CMD",
    PROTO_ERR_BAD_LENGTH:    "BAD_LENGTH",
    PROTO_ERR_BUSY:          "BUSY",
    PROTO_ERR_FLASH:         "FLASH",
    PROTO_ERR_HEALTH_FAIL:   "HEALTH_FAIL",
    PROTO_ERR_HEALTH_WARM:   "HEALTH_WARMING",
    PROTO_ERR_NO_ENTROPY:    "NO_ENTROPY",
    PROTO_ERR_INTERNAL:      "INTERNAL",
}

LED_STATE_NAMES = {
    0: "BOOT",
    1: "NO_FPGA",
    2: "IDLE",
    3: "BUSY",
    4: "PROGRAM",
    5: "ERROR",
}

HEALTH_STATE_NAMES = {
    0: "WARMING",
    1: "OK",
    2: "FAIL_RCT",
    3: "FAIL_APT",
}

# Per-request limit (firmware: RANDOM_PER_CALL_MAX in protocol.c).
RANDOM_REQ_MAX = 256
# Page-aligned chunk sent per FLASH_DATA frame.
FLASH_CHUNK    = 256


# ── USB plumbing ────────────────────────────────────────────────────────

class CriptoliteDevice:
    """Thin wrapper around the bulk endpoints exposing the protocol frames."""

    def __init__(self, timeout_ms: int = 5000):
        self.timeout_ms = timeout_ms
        self._rx_overflow = bytearray()  # leftover bytes from previous reads
        self.dev = usb.core.find(idVendor=VID, idProduct=PID)
        if self.dev is None:
            raise RuntimeError(
                f"Device {VID:04x}:{PID:04x} not found. Is the board plugged in?"
            )
        # Some kernels claim Vendor interfaces with usbfs; detach if so.
        try:
            if self.dev.is_kernel_driver_active(0):
                self.dev.detach_kernel_driver(0)
        except (NotImplementedError, usb.core.USBError):
            pass

        # Only call set_configuration() the first time. Calling it again on
        # an already-configured device resets the endpoints on the firmware
        # side, which has been observed to wedge subsequent reads on this
        # vendor implementation.
        try:
            cfg = self.dev.get_active_configuration()
        except usb.core.USBError:
            cfg = None
        if cfg is None:
            self.dev.set_configuration()
            cfg = self.dev.get_active_configuration()

        intf = cfg[(0, 0)]
        usb.util.claim_interface(self.dev, intf.bInterfaceNumber)
        self.intf = intf

    def close(self):
        try:
            usb.util.release_interface(self.dev, self.intf.bInterfaceNumber)
        except usb.core.USBError:
            pass
        usb.util.dispose_resources(self.dev)

    # Low-level send: header + payload as a single transfer (libusb splits).
    def _send_request(self, cmd: int, payload: bytes = b""):
        if len(payload) > 0xFFFF:
            raise ValueError("payload too long")
        header = bytes([cmd, 0x00, len(payload) & 0xFF, (len(payload) >> 8) & 0xFF])
        self.dev.write(EP_OUT, header + payload, timeout=self.timeout_ms)

    def _read_exact(self, n: int, timeout_ms: int | None = None) -> bytes:
        """Read exactly n bytes from the IN endpoint.

        libusb's bulk_transfer fills the whole supplied buffer in one call but
        will overflow if the device sends a packet larger than that buffer.
        To keep things safe we always issue reads of >= one max-packet-size
        worth, store the remainder, and serve future small reads from there.
        """
        timeout = timeout_ms if timeout_ms is not None else self.timeout_ms
        out = bytearray()
        # First, take whatever was left over from a previous read.
        if self._rx_overflow:
            take = min(n, len(self._rx_overflow))
            out.extend(self._rx_overflow[:take])
            del self._rx_overflow[:take]

        while len(out) < n:
            need = n - len(out)
            # Read in multiples of the endpoint's wMaxPacketSize so libusb
            # never reports ERR_OVERFLOW on a short device-to-host packet.
            buf_len = ((need + EP_IN_MAX_PKT - 1) // EP_IN_MAX_PKT) * EP_IN_MAX_PKT
            chunk = self.dev.read(EP_IN, buf_len, timeout=timeout)
            if len(chunk) <= need:
                out.extend(chunk)
            else:
                out.extend(chunk[:need])
                self._rx_overflow.extend(chunk[need:])
        return bytes(out)

    def _read_response_header(self, timeout_ms: int | None = None):
        """Read 4-byte header → (cmd, status, payload_len)."""
        h = self._read_exact(4, timeout_ms=timeout_ms)
        return h[0], h[1], h[2] | (h[3] << 8)

    # ── Commands ──

    def status(self):
        self._send_request(CMD_STATUS)
        cmd, status, plen = self._read_response_header()
        if cmd != CMD_STATUS or status != PROTO_OK:
            raise RuntimeError(f"STATUS failed: {ERROR_NAMES.get(status, status)}")
        body = self._read_exact(plen)
        cdone, led_state, health, paused = body[0], body[1], body[2], body[3]
        bits_seen = struct.unpack_from("<I", body, 4)[0]
        rct_max   = struct.unpack_from("<H", body, 8)[0]
        apt_max   = struct.unpack_from("<H", body, 10)[0]
        return dict(
            cdone=bool(cdone),
            led_state=LED_STATE_NAMES.get(led_state, str(led_state)),
            health=HEALTH_STATE_NAMES.get(health, str(health)),
            paused=bool(paused),
            bits_seen=bits_seen,
            rct_max=rct_max,
            apt_max=apt_max,
        )

    def get_random(self, n: int) -> bytes:
        """Read exactly n bytes by issuing as many GET_RANDOM requests as needed."""
        out = bytearray()
        while len(out) < n:
            chunk = min(n - len(out), RANDOM_REQ_MAX)
            self._send_request(CMD_GET_RANDOM, struct.pack("<I", chunk))
            cmd, status, plen = self._read_response_header(timeout_ms=10000)
            if cmd != CMD_GET_RANDOM or status != PROTO_OK:
                raise RuntimeError(
                    f"GET_RANDOM failed: {ERROR_NAMES.get(status, status)}"
                )
            if plen != chunk:
                raise RuntimeError(
                    f"GET_RANDOM length mismatch: asked {chunk}, got {plen}"
                )
            out.extend(self._read_exact(plen, timeout_ms=20000))
        return bytes(out)

    def flash_begin(self, total_size: int):
        self._send_request(CMD_FLASH_BEGIN, struct.pack("<I", total_size))
        # Erasing a 4 MB region can take a few seconds; the firmware itself
        # has a 30 s safety timeout per status poll. Keep this generous.
        cmd, status, plen = self._read_response_header(timeout_ms=60000)
        if status != PROTO_OK:
            raise RuntimeError(
                f"FLASH_BEGIN failed: {ERROR_NAMES.get(status, status)}"
            )
        # Drain any payload (none expected)
        if plen:
            self._read_exact(plen)

    def flash_data(self, offset: int, data: bytes):
        self._send_request(CMD_FLASH_DATA, struct.pack("<I", offset) + data)
        cmd, status, plen = self._read_response_header()
        if status != PROTO_OK:
            raise RuntimeError(
                f"FLASH_DATA failed @ {offset:#x}: {ERROR_NAMES.get(status, status)}"
            )
        if plen:
            self._read_exact(plen)

    def flash_end(self) -> bool:
        self._send_request(CMD_FLASH_END, struct.pack("<I", 0))
        cmd, status, plen = self._read_response_header(timeout_ms=10000)
        if status != PROTO_OK:
            raise RuntimeError(
                f"FLASH_END failed: {ERROR_NAMES.get(status, status)}"
            )
        body = self._read_exact(plen) if plen else b""
        cdone = bool(body[0]) if body else False
        return cdone

    def reboot_bootloader(self):
        self._send_request(CMD_REBOOT_BOOTLOADER)
        try:
            cmd, status, plen = self._read_response_header(timeout_ms=2000)
            if plen:
                self._read_exact(plen)
        except usb.core.USBError:
            # Device disappears mid-response; that's expected.
            pass


# ── CLI helpers ─────────────────────────────────────────────────────────

def confirm(prompt: str) -> bool:
    sys.stderr.write(f"{prompt} [y/N]: ")
    sys.stderr.flush()
    ans = sys.stdin.readline().strip().lower()
    return ans in ("y", "yes")


def find_rpi_rp2_mountpoint(timeout_s: float = 15.0) -> Path | None:
    """Poll /media/$USER and /run/media/$USER until RPI-RP2 shows up."""
    user = os.environ.get("USER", "")
    candidates = [
        Path(f"/media/{user}/RPI-RP2"),
        Path(f"/run/media/{user}/RPI-RP2"),
        Path("/media/RPI-RP2"),
    ]
    deadline = time.time() + timeout_s
    while time.time() < deadline:
        for p in candidates:
            if p.is_dir():
                return p
        time.sleep(0.5)
    return None


# ── Sub-commands ────────────────────────────────────────────────────────

def cmd_status(args):
    dev = CriptoliteDevice()
    try:
        s = dev.status()
    finally:
        dev.close()
    print(f"  CDONE      : {'1 (configured)' if s['cdone'] else '0 (not configured)'}")
    print(f"  LED state  : {s['led_state']}")
    print(f"  Health     : {s['health']}")
    print(f"  TRNG paused: {s['paused']}")
    print(f"  Bits seen  : {s['bits_seen']}")
    print(f"  RCT max    : {s['rct_max']}")
    print(f"  APT max    : {s['apt_max']}")


def cmd_random(args):
    if args.bits is not None and args.n is not None:
        sys.exit("Use either -n (bytes) or --bits, not both.")
    if args.bits is not None:
        n_bytes = math.ceil(args.bits / 8)
    else:
        n_bytes = args.n if args.n is not None else 32

    if n_bytes <= 0:
        sys.exit("Length must be > 0.")

    dev = CriptoliteDevice()
    try:
        data = dev.get_random(n_bytes)
    finally:
        dev.close()

    if args.output:
        if args.output == "-":
            sys.stdout.buffer.write(data)
        else:
            Path(args.output).write_bytes(data)
            print(f"  Wrote {len(data)} bytes to {args.output}", file=sys.stderr)
    else:
        # Default: print hex to stdout
        print(data.hex())


def cmd_update_fpga(args):
    bitstream = Path(args.bitstream)
    if not bitstream.is_file():
        sys.exit(f"Bitstream not found: {bitstream}")
    size = bitstream.stat().st_size
    print(f"FPGA bitstream : {bitstream}  ({size} bytes)", file=sys.stderr)
    if not args.yes and not confirm(
            "This will erase and rewrite the FPGA flash. Proceed?"):
        sys.exit("Aborted.")

    dev = CriptoliteDevice()
    try:
        print("  Erasing flash and starting session...", file=sys.stderr)
        dev.flash_begin(size)

        data = bitstream.read_bytes()
        print(f"  Programming {len(data)} bytes "
              f"({math.ceil(len(data) / FLASH_CHUNK)} chunks)...", file=sys.stderr)

        for offset in range(0, len(data), FLASH_CHUNK):
            chunk = data[offset:offset + FLASH_CHUNK]
            dev.flash_data(offset, chunk)
            if (offset // FLASH_CHUNK) % 32 == 0:
                pct = 100 * offset / len(data)
                sys.stderr.write(f"\r    {pct:5.1f}% ({offset}/{len(data)})")
                sys.stderr.flush()
        sys.stderr.write(f"\r    100.0% ({len(data)}/{len(data)})\n")

        print("  Verifying & rebooting FPGA...", file=sys.stderr)
        cdone = dev.flash_end()
        if cdone:
            print("  OK — FPGA reconfigured (CDONE=1).", file=sys.stderr)
        else:
            print("  WARNING — FPGA failed to configure (CDONE=0).", file=sys.stderr)
            sys.exit(2)
    finally:
        dev.close()


def cmd_update_rp2040(args):
    uf2 = Path(args.firmware)
    if not uf2.is_file():
        sys.exit(f"UF2 not found: {uf2}")
    print(f"RP2040 firmware: {uf2}  ({uf2.stat().st_size} bytes)", file=sys.stderr)
    if not args.yes and not confirm(
            "This will reboot the RP2040 to USB bootloader and overwrite firmware. Proceed?"):
        sys.exit("Aborted.")

    dev = CriptoliteDevice()
    try:
        dev.reboot_bootloader()
    finally:
        dev.close()

    print("  Waiting for RPI-RP2 mass-storage device...", file=sys.stderr)
    mount = find_rpi_rp2_mountpoint(timeout_s=20)
    if mount is None:
        sys.exit("RPI-RP2 did not mount within the timeout. "
                 "Copy the .uf2 manually from your file manager.")
    print(f"  Mounted at {mount}", file=sys.stderr)

    dest = mount / uf2.name
    shutil.copyfile(uf2, dest)
    # Force a flush so the BOOTSEL ROM picks up the file.
    try:
        os.sync()
    except AttributeError:
        pass
    print(f"  Copied {uf2.name} → {mount}. The board will reboot.", file=sys.stderr)


# ── argparse ───────────────────────────────────────────────────────────

def main():
    ap = argparse.ArgumentParser(
        description="CryptoLite-ICE host client (USB Vendor protocol).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = ap.add_subparsers(dest="cmd", required=True)

    sub.add_parser("status", help="Print board status")

    p_random = sub.add_parser("random", help="Generate random bytes")
    p_random.add_argument("-n", type=int, default=None,
                          help="Number of bytes (default: 32 if neither -n nor --bits given)")
    p_random.add_argument("--bits", type=int, default=None,
                          help="Number of bits (rounded up to whole bytes)")
    p_random.add_argument("-o", "--output", default=None,
                          help="Write raw bytes to file (use '-' for stdout). "
                               "If omitted, prints hex to stdout.")

    p_ufpga = sub.add_parser("update-fpga", help="Reprogram the FPGA bitstream")
    p_ufpga.add_argument("bitstream", help="Path to .bin bitstream")
    p_ufpga.add_argument("-y", "--yes", action="store_true",
                         help="Skip confirmation prompt")

    p_urp = sub.add_parser("update-rp2040", help="Reboot to bootloader and copy a .uf2")
    p_urp.add_argument("firmware", help="Path to .uf2 firmware")
    p_urp.add_argument("-y", "--yes", action="store_true",
                       help="Skip confirmation prompt")

    args = ap.parse_args()

    handlers = {
        "status":         cmd_status,
        "random":         cmd_random,
        "update-fpga":    cmd_update_fpga,
        "update-rp2040":  cmd_update_rp2040,
    }
    handlers[args.cmd](args)


if __name__ == "__main__":
    main()
