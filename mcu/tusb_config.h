/*
 * tusb_config.h — TinyUSB config for CryptoLite-RP firmware
 *   Vendor class only (no CDC, no MSC). Two bulk endpoints carry the
 *   binary command protocol used by criptolite-ice.py.
 * controlpaths.com | lut7.dev
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// ── Common ────────────────────────────────────────────────────────────────
#define CFG_TUSB_RHPORT0_MODE    (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)
#define CFG_TUSB_OS              OPT_OS_PICO
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN       __attribute__((aligned(4)))

// ── Device ────────────────────────────────────────────────────────────────
#define CFG_TUD_ENABLED          1
#define CFG_TUD_ENDPOINT0_SIZE   64

// Class drivers — enable Vendor only
#define CFG_TUD_CDC              0
#define CFG_TUD_MSC              0
#define CFG_TUD_HID              0
#define CFG_TUD_MIDI             0
#define CFG_TUD_VENDOR           1

// Vendor FIFO sizes (bytes). RX: holds one flash-program command (256 B
// page + small header). TX: 512 B is plenty — _send_response keeps
// pumping tud_task() to drain the FIFO when it fills up.
#define CFG_TUD_VENDOR_RX_BUFSIZE   512
#define CFG_TUD_VENDOR_TX_BUFSIZE   512
#define CFG_TUD_VENDOR_EPSIZE       64

#ifdef __cplusplus
}
#endif
