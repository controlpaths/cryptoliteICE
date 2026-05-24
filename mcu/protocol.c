/*
 * protocol.c — Binary command protocol over USB Vendor for CryptoLite-RP
 * controlpaths.com | lut7.dev
 *
 * Frame parser sits on top of TinyUSB's vendor RX FIFO. Each iteration of
 * protocol_task() pulls everything currently buffered, then tries to peel
 * off as many complete frames as possible. Long-running commands (flash
 * erase, large RNG streams) keep tud_task() running so the USB stack stays
 * alive while the dispatch routine is busy.
 */

#include "protocol.h"
#include "trng.h"
#include "rng_pipeline.h"
#include "nist_health.h"
#include "sha256.h"
#include "ice40.h"
#include "flash_writer.h"
#include "led_status.h"

#include "tusb.h"
#include "pico/bootrom.h"
#include "pico/time.h"
#include <string.h>

#define HEADER_BYTES        4
#define RX_BUF_BYTES        512u
/* Maximum bytes the firmware will produce per single GET_RANDOM call.
 * The host iterates if more are requested. Sized small (256 B) so the
 * response always fits in a few USB packets and the dispatcher stays
 * responsive even when the host is hammering it. */
#define RANDOM_PER_CALL_MAX 256u

/* ── Frame buffer ────────────────────────────────────────────────────── */
static uint8_t  _rx[RX_BUF_BYTES];
static uint16_t _rx_len = 0;

/* ── Helpers ─────────────────────────────────────────────────────────── */

static inline uint16_t _ld_u16le(const uint8_t *p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static inline uint32_t _ld_u32le(const uint8_t *p) {
    return (uint32_t)p[0]
         | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16)
         | ((uint32_t)p[3] << 24);
}

/* Push a fixed-format response. Pumps tud_task() while the FIFO drains so
 * the host's reads make progress. Bails out if the FIFO refuses to drain
 * for over 1 second (e.g. host disconnected mid-transfer) — better to drop
 * the response than to wedge the dispatcher forever. */
static void _send_response(uint8_t cmd, uint8_t status,
                           const uint8_t *payload, uint16_t payload_len) {
    uint8_t hdr[HEADER_BYTES];
    hdr[0] = cmd;
    hdr[1] = status;
    hdr[2] = (uint8_t)(payload_len & 0xFF);
    hdr[3] = (uint8_t)((payload_len >> 8) & 0xFF);

    uint32_t deadline = to_ms_since_boot(get_absolute_time()) + 1000;

    uint16_t written = 0;
    while (written < HEADER_BYTES) {
        uint32_t n = tud_vendor_write(hdr + written, HEADER_BYTES - written);
        written += (uint16_t)n;
        if (n == 0) {
            tud_task();
            if (to_ms_since_boot(get_absolute_time()) > deadline) return;
        }
    }
    if (payload != NULL && payload_len > 0) {
        written = 0;
        while (written < payload_len) {
            uint32_t n = tud_vendor_write(payload + written, payload_len - written);
            written += (uint16_t)n;
            if (n == 0) {
                tud_task();
                if (to_ms_since_boot(get_absolute_time()) > deadline) return;
            }
        }
    }
    tud_vendor_write_flush();
}

/* ── Per-command handlers ────────────────────────────────────────────── */

static void _cmd_status(const uint8_t *payload, uint16_t plen) {
    (void)payload; (void)plen;

    uint32_t bits_seen, rct_max, apt_max;
    nist_health_get_stats(&bits_seen, &rct_max, &apt_max);

    uint8_t resp[12];
    resp[0]  = ice40_is_configured() ? 1 : 0;
    resp[1]  = (uint8_t)led_status_get();
    resp[2]  = (uint8_t)nist_health_status();
    resp[3]  = trng_is_paused() ? 1 : 0;
    resp[4]  = (uint8_t)(bits_seen & 0xFF);
    resp[5]  = (uint8_t)((bits_seen >> 8) & 0xFF);
    resp[6]  = (uint8_t)((bits_seen >> 16) & 0xFF);
    resp[7]  = (uint8_t)((bits_seen >> 24) & 0xFF);
    resp[8]  = (uint8_t)(rct_max & 0xFF);
    resp[9]  = (uint8_t)((rct_max >> 8) & 0xFF);
    resp[10] = (uint8_t)(apt_max & 0xFF);
    resp[11] = (uint8_t)((apt_max >> 8) & 0xFF);

    _send_response(PROTO_CMD_STATUS, PROTO_OK, resp, sizeof(resp));
}

static void _cmd_get_random(const uint8_t *payload, uint16_t plen) {
    if (plen != 4) {
        _send_response(PROTO_CMD_GET_RANDOM, PROTO_ERR_BAD_LENGTH, NULL, 0);
        return;
    }
    if (flash_writer_in_progress() || trng_is_paused()) {
        _send_response(PROTO_CMD_GET_RANDOM, PROTO_ERR_BUSY, NULL, 0);
        return;
    }

    uint32_t n = _ld_u32le(payload);
    if (n == 0 || n > RANDOM_PER_CALL_MAX) {
        _send_response(PROTO_CMD_GET_RANDOM, PROTO_ERR_BAD_LENGTH, NULL, 0);
        return;
    }

    /* Inline pipeline: 16 raw bytes → NIST health feed → SHA-256 → 32
     * conditioned bytes. Repeats until n bytes have been produced. This
     * mirrors what rng_pipeline_get_bytes() does but skips the extra
     * pool / wait_for_pool layers, which were causing post-call hangs.
     */
    static uint8_t buf[RANDOM_PER_CALL_MAX];
    led_status_set(LED_STATE_BUSY);
    uint32_t produced = 0;
    while (produced < n) {
        /* Wait for 16 raw bytes in the ring buffer. trng_task() is the
         * only producer; pump it (and tud_task) here so we don't starve
         * USB while the SPI master refills. */
        uint8_t raw[16];
        uint32_t t0 = to_ms_since_boot(get_absolute_time());
        while (trng_available_bytes() < sizeof(raw)) {
            tud_task();
            trng_task();
            if (to_ms_since_boot(get_absolute_time()) - t0 > 2000) {
                led_status_set(LED_STATE_IDLE);
                _send_response(PROTO_CMD_GET_RANDOM,
                               PROTO_ERR_NO_ENTROPY, NULL, 0);
                return;
            }
        }
        trng_read_bytes(raw, sizeof(raw));

        /* Health tests are still updated continuously by rng_pipeline_task()
         * in the main loop; just check the current verdict without feeding
         * the same bytes twice (that would inflate _bits_seen). */
        nist_health_status_t hs = nist_health_status();
        if (hs == NIST_HEALTH_FAIL_RCT || hs == NIST_HEALTH_FAIL_APT) {
            led_status_set(LED_STATE_IDLE);
            _send_response(PROTO_CMD_GET_RANDOM, PROTO_ERR_HEALTH_FAIL,
                           NULL, 0);
            return;
        }
        if (hs == NIST_HEALTH_WARMING) {
            led_status_set(LED_STATE_IDLE);
            _send_response(PROTO_CMD_GET_RANDOM, PROTO_ERR_HEALTH_WARMING,
                           NULL, 0);
            return;
        }

        /* Conditioning: 16 raw → SHA-256 → 32 conditioned bytes. */
        uint8_t hash[32];
        sha256(raw, sizeof(raw), hash);
        uint32_t take = n - produced;
        if (take > sizeof(hash)) take = sizeof(hash);
        for (uint32_t i = 0; i < take; i++) buf[produced + i] = hash[i];
        produced += take;
    }

    _send_response(PROTO_CMD_GET_RANDOM, PROTO_OK, buf, (uint16_t)n);
    led_status_set(LED_STATE_IDLE);
}

static void _cmd_flash_begin(const uint8_t *payload, uint16_t plen) {
    if (plen != 4) {
        _send_response(PROTO_CMD_FLASH_BEGIN, PROTO_ERR_BAD_LENGTH, NULL, 0);
        return;
    }
    uint32_t total = _ld_u32le(payload);

    fw_status_t s = flash_writer_begin(total);
    uint8_t st = PROTO_OK;
    if (s == FW_ERR_BUSY)        st = PROTO_ERR_BUSY;
    else if (s == FW_ERR_RANGE)  st = PROTO_ERR_BAD_LENGTH;
    else if (s != FW_OK)         st = PROTO_ERR_FLASH;

    _send_response(PROTO_CMD_FLASH_BEGIN, st, NULL, 0);
}

static void _cmd_flash_data(const uint8_t *payload, uint16_t plen) {
    if (plen < 4) {
        _send_response(PROTO_CMD_FLASH_DATA, PROTO_ERR_BAD_LENGTH, NULL, 0);
        return;
    }
    uint32_t offset = _ld_u32le(payload);
    fw_status_t s = flash_writer_data(offset, payload + 4, plen - 4);
    uint8_t st = PROTO_OK;
    if (s == FW_ERR_NOT_BEGUN)   st = PROTO_ERR_BUSY;
    else if (s == FW_ERR_RANGE)  st = PROTO_ERR_BAD_LENGTH;
    else if (s != FW_OK)         st = PROTO_ERR_FLASH;

    _send_response(PROTO_CMD_FLASH_DATA, st, NULL, 0);
}

static void _cmd_flash_end(const uint8_t *payload, uint16_t plen) {
    uint32_t crc = (plen >= 4) ? _ld_u32le(payload) : 0u;
    bool cdone = false;
    fw_status_t s = flash_writer_end(crc, &cdone);
    uint8_t st = (s == FW_OK) ? PROTO_OK : PROTO_ERR_FLASH;

    uint8_t resp[1] = { cdone ? 1u : 0u };
    _send_response(PROTO_CMD_FLASH_END, st, resp, sizeof(resp));
}

static void _cmd_reboot_bootloader(const uint8_t *payload, uint16_t plen) {
    (void)payload; (void)plen;
    _send_response(PROTO_CMD_REBOOT_BOOTLOADER, PROTO_OK, NULL, 0);
    /* Same recipe as the old CDC firmware's AT+UMCU: brief pause to let
     * the IN endpoint drain, then jump to BOOTSEL ROM. Calling tud_task()
     * in a tight loop here can leave a transfer in flight when the reset
     * fires, which sometimes confuses the host enumerator. */
    sleep_ms(200);
    reset_usb_boot(0, 0);
    /* Does not return. */
}

/* ── Frame dispatcher ────────────────────────────────────────────────── */

static void _dispatch(uint8_t cmd, const uint8_t *payload, uint16_t plen) {
    switch (cmd) {
        case PROTO_CMD_STATUS:            _cmd_status(payload, plen);            break;
        case PROTO_CMD_GET_RANDOM:        _cmd_get_random(payload, plen);        break;
        case PROTO_CMD_FLASH_BEGIN:       _cmd_flash_begin(payload, plen);       break;
        case PROTO_CMD_FLASH_DATA:        _cmd_flash_data(payload, plen);        break;
        case PROTO_CMD_FLASH_END:         _cmd_flash_end(payload, plen);         break;
        case PROTO_CMD_REBOOT_BOOTLOADER: _cmd_reboot_bootloader(payload, plen); break;
        default:
            _send_response(cmd, PROTO_ERR_BAD_CMD, NULL, 0);
            break;
    }
}

/* ── Public ──────────────────────────────────────────────────────────── */

void protocol_init(void) {
    _rx_len = 0;
}

void protocol_task(void) {
    /* Drain RX FIFO into the local buffer. */
    while (tud_vendor_available()) {
        if (_rx_len >= RX_BUF_BYTES) break;
        uint32_t room = RX_BUF_BYTES - _rx_len;
        uint32_t got  = tud_vendor_read(_rx + _rx_len, room);
        if (got == 0) break;
        _rx_len += (uint16_t)got;
    }

    /* Peel off complete frames. */
    while (_rx_len >= HEADER_BYTES) {
        uint16_t plen = _ld_u16le(_rx + 2);
        uint16_t total = HEADER_BYTES + plen;
        if (total > RX_BUF_BYTES) {
            /* Frame too big to ever buffer — drop everything. */
            _rx_len = 0;
            _send_response(_rx[0], PROTO_ERR_BAD_LENGTH, NULL, 0);
            break;
        }
        if (_rx_len < total) break;

        _dispatch(_rx[0], _rx + HEADER_BYTES, plen);

        memmove(_rx, _rx + total, _rx_len - total);
        _rx_len -= total;
    }
}
