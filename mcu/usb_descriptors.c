/*
 * usb_descriptors.c — USB descriptors for CryptoLite-RP firmware
 *   Single Vendor interface, two bulk endpoints (IN/OUT). The host driver
 *   for this device is libusb (used by criptolite-ice.py).
 * controlpaths.com | lut7.dev
 */

#include "tusb.h"
#include "pico/unique_id.h"
#include <string.h>

// ── VID / PID ────────────────────────────────────────────────────────────
// PID kept identical to the CDC+MSC firmware (see CLAUDE.md), but the device
// now exposes a Vendor-only interface so libusb does not collide with the
// kernel CDC driver.
#define USBD_VID        0x2E8A  // Raspberry Pi
#define USBD_PID        0x000F  // CryptoLite-ICE
#define USBD_BCD_DEV    0x0200  // version 2.0

// ── Interface numbers ─────────────────────────────────────────────────────
enum {
    ITF_VENDOR = 0,
    ITF_TOTAL
};

// ── Endpoint addresses ────────────────────────────────────────────────────
#define EP_VENDOR_OUT   0x01
#define EP_VENDOR_IN    0x81

// ── String indices ────────────────────────────────────────────────────────
enum {
    STR_IDX_LANGID = 0,
    STR_IDX_MANUFACTURER,
    STR_IDX_PRODUCT,
    STR_IDX_SERIAL,
    STR_IDX_VENDOR_IFACE,
    STR_IDX_COUNT
};

// ── Descriptor lengths ────────────────────────────────────────────────────
#define USBD_DESC_LEN   (TUD_CONFIG_DESC_LEN + TUD_VENDOR_DESC_LEN)

// ── Device descriptor ─────────────────────────────────────────────────────
static const tusb_desc_device_t desc_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,
    .bDeviceClass       = 0x00,
    .bDeviceSubClass    = 0x00,
    .bDeviceProtocol    = 0x00,
    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USBD_VID,
    .idProduct          = USBD_PID,
    .bcdDevice          = USBD_BCD_DEV,
    .iManufacturer      = STR_IDX_MANUFACTURER,
    .iProduct           = STR_IDX_PRODUCT,
    .iSerialNumber      = STR_IDX_SERIAL,
    .bNumConfigurations = 1,
};

// ── Configuration descriptor ─────────────────────────────────────────────
static const uint8_t desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(
        1,              // config number
        ITF_TOTAL,      // interface count
        0,              // string index
        USBD_DESC_LEN,  // total length
        0x00,           // attributes (bus powered)
        250             // max power (mA)
    ),
    TUD_VENDOR_DESCRIPTOR(
        ITF_VENDOR,
        STR_IDX_VENDOR_IFACE,
        EP_VENDOR_OUT,
        EP_VENDOR_IN,
        64
    ),
};

// ── String descriptors ────────────────────────────────────────────────────
static char serial_str[PICO_UNIQUE_BOARD_ID_SIZE_BYTES * 2 + 1];

static const char *const string_desc_arr[] = {
    [STR_IDX_MANUFACTURER] = "controlpaths.com",
    [STR_IDX_PRODUCT]      = "CryptoLite-RP",
    [STR_IDX_SERIAL]       = serial_str,
    [STR_IDX_VENDOR_IFACE] = "CryptoLite-RP Vendor",
};

// ── Callbacks ─────────────────────────────────────────────────────────────

const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&desc_device;
}

const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return desc_configuration;
}

const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    static uint16_t desc_str[32];

    if (index == STR_IDX_LANGID) {
        desc_str[0] = (TUSB_DESC_STRING << 8) | 4;
        desc_str[1] = 0x0409;   // English (US)
        return desc_str;
    }

    if (index == STR_IDX_SERIAL) {
        pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
    }

    if (index >= STR_IDX_COUNT) return NULL;

    const char *s = string_desc_arr[index];
    size_t len = strlen(s);
    if (len > 31) len = 31;

    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 + 2 * len));
    for (size_t i = 0; i < len; i++) {
        desc_str[1 + i] = (uint16_t)s[i];
    }

    return desc_str;
}
