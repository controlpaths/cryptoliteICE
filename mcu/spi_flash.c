/*
 * spi_flash.c — W25Q32 SPI flash driver for CryptoLite-ICE
 * controlpaths.com | lut7.dev
 */

#include "spi_flash.h"
#include "spi_pio.h"
#include "pins.h"
#include "tusb.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include "pico/time.h"
#include <string.h>

// ── W25Q32 command opcodes ────────────────────────────────────────────────
#define CMD_WRITE_ENABLE    0x06
#define CMD_READ_STATUS1    0x05
#define CMD_READ_STATUS2    0x35
#define CMD_WRITE_STATUS    0x01
#define CMD_READ_DATA       0x03
#define CMD_PAGE_PROGRAM    0x02
#define CMD_SECTOR_ERASE    0x20
#define CMD_BLOCK_ERASE     0xD8
#define CMD_JEDEC_ID        0x9F
#define CMD_RELEASE_DPD     0xAB   // Release from Deep Power Down (W25Q32JV §8.2.24)

#define STATUS_BUSY_BIT     0x01

// ── CS control ────────────────────────────────────────────────────────────
static inline void cs_assert(void)   { gpio_put(PIN_ICE_SSn, 0); }
static inline void cs_deassert(void) { gpio_put(PIN_ICE_SSn, 1); }

// ── Bit-bang helpers (bypasses PIO — used for JEDEC init) ─────────────────

static void _bb_byte_out(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        gpio_put(PIN_ICE_SO, (byte >> i) & 1);
        busy_wait_us(2);
        gpio_put(PIN_ICE_SCK, 1);
        busy_wait_us(2);
        gpio_put(PIN_ICE_SCK, 0);
        busy_wait_us(2);
    }
}

static uint8_t _bb_byte_in(void) {
    uint8_t val = 0;
    for (int i = 7; i >= 0; i--) {
        gpio_put(PIN_ICE_SCK, 1);
        busy_wait_us(2);
        if (gpio_get(PIN_ICE_SI)) val |= (1u << i);
        gpio_put(PIN_ICE_SCK, 0);
        busy_wait_us(2);
    }
    return val;
}

// Release flash from Deep Power Down.  The FPGA bitstream commonly sends
// the flash to DPD (0xB9) after reading its bitstream.  Call this once before
// any flash access; tRES1 = 3 µs min (W25Q32JV).  Pins must be SIO at entry.
static void _release_from_dpd(void) {
    gpio_set_function(PIN_ICE_SCK, GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SCK, GPIO_OUT);
    gpio_set_function(PIN_ICE_SO,  GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SO,  GPIO_OUT);
    gpio_set_function(PIN_ICE_SI,  GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SI,  GPIO_IN);
    gpio_pull_down(PIN_ICE_SI);
    gpio_put(PIN_ICE_SCK, 0);
    gpio_put(PIN_ICE_SO,  0);
    gpio_put(PIN_ICE_SSn, 0);
    busy_wait_us(2);
    _bb_byte_out(CMD_RELEASE_DPD);
    gpio_put(PIN_ICE_SSn, 1);
    busy_wait_us(100);   // tRES1 = 3 µs; 100 µs gives ample margin
}

static bool _bitbang_jedec(uint8_t *id) {
    gpio_set_function(PIN_ICE_SCK, GPIO_FUNC_SIO);
    gpio_set_function(PIN_ICE_SO,  GPIO_FUNC_SIO);
    gpio_set_function(PIN_ICE_SI,  GPIO_FUNC_SIO);
    gpio_set_dir(PIN_ICE_SCK, GPIO_OUT);
    gpio_set_dir(PIN_ICE_SO,  GPIO_OUT);
    gpio_set_dir(PIN_ICE_SI,  GPIO_IN);
    gpio_pull_down(PIN_ICE_SI);
    gpio_put(PIN_ICE_SCK, 0);
    gpio_put(PIN_ICE_SO,  0);

    busy_wait_us(50);
    g_flash_miso_idle = gpio_get(PIN_ICE_SI);

    gpio_put(PIN_ICE_SSn, 0);
    busy_wait_us(2);
    _bb_byte_out(CMD_JEDEC_ID);
    id[0] = _bb_byte_in();
    id[1] = _bb_byte_in();
    id[2] = _bb_byte_in();
    gpio_put(PIN_ICE_SSn, 1);

    spi_pio_init();

    return (id[0] == FLASH_JEDEC_MANUF &&
            id[1] == FLASH_JEDEC_TYPE  &&
            id[2] == FLASH_JEDEC_CAPACITY);
}

// ── Internal helpers ──────────────────────────────────────────────────────

static void _write_enable(void) {
    uint8_t cmd = CMD_WRITE_ENABLE;
    cs_assert();
    spi_pio_write(&cmd, 1);
    cs_deassert();
}

static uint8_t _read_sr1(void) {
    uint8_t cmd = CMD_READ_STATUS1;
    uint8_t sr  = 0;
    cs_assert();
    spi_pio_write(&cmd, 1);
    spi_pio_read(&sr, 1);
    cs_deassert();
    return sr;
}

static uint8_t _read_sr2(void) {
    uint8_t cmd = CMD_READ_STATUS2;
    uint8_t sr  = 0;
    cs_assert();
    spi_pio_write(&cmd, 1);
    spi_pio_read(&sr, 1);
    cs_deassert();
    return sr;
}

static void _wait_ready(void) {
    // Bit-bang status polling — avoids PIO RX timing issues during erase/write
    gpio_set_function(PIN_ICE_SCK, GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SCK, GPIO_OUT);
    gpio_set_function(PIN_ICE_SO,  GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SO,  GPIO_OUT);
    gpio_set_function(PIN_ICE_SI,  GPIO_FUNC_SIO); gpio_set_dir(PIN_ICE_SI,  GPIO_IN);
    gpio_pull_down(PIN_ICE_SI);
    gpio_put(PIN_ICE_SCK, 0);
    gpio_put(PIN_ICE_SO,  0);

    uint8_t  status;
    uint32_t last_tud = to_ms_since_boot(get_absolute_time());
    uint32_t t0       = last_tud;
    do {
        gpio_put(PIN_ICE_SSn, 0);
        _bb_byte_out(CMD_READ_STATUS1);
        status = _bb_byte_in();
        gpio_put(PIN_ICE_SSn, 1);
        busy_wait_us(500);
        uint32_t now = to_ms_since_boot(get_absolute_time());
        if (now - last_tud >= 5) { tud_task(); last_tud = now; }
        if (now - t0 > 1000) break;    // 1 s safety timeout (per call)
    } while (status & STATUS_BUSY_BIT);

    spi_pio_init();
}

void flash_unprotect(void) {
    _write_enable();
    uint8_t pkt[3] = { CMD_WRITE_STATUS, 0x00, 0x00 };
    cs_assert();
    spi_pio_write(pkt, 3);
    cs_deassert();
    _wait_ready();
}

static void _send_addr(uint32_t addr) {
    uint8_t a[3] = {
        (uint8_t)((addr >> 16) & 0xFF),
        (uint8_t)((addr >>  8) & 0xFF),
        (uint8_t)( addr        & 0xFF),
    };
    spi_pio_write(a, 3);
}

// ── Public API ────────────────────────────────────────────────────────────

uint8_t g_flash_jedec[3]  = {0, 0, 0};
bool    g_flash_miso_idle = false;
uint8_t g_flash_sr1       = 0xFF;
uint8_t g_flash_sr2       = 0xFF;

bool flash_init(void) {
    gpio_init(PIN_ICE_SSn);
    gpio_set_dir(PIN_ICE_SSn, GPIO_OUT);
    gpio_put(PIN_ICE_SSn, 1);

    // Wake flash from Deep Power Down before any JEDEC or status read.
    // The iCE40 bitstream often puts the flash in DPD (0xB9) after loading
    // its configuration; without this the flash SO stays HiZ and JEDEC = FF.
    _release_from_dpd();

    if (_bitbang_jedec(g_flash_jedec)) {
        g_flash_sr1 = _read_sr1();
        g_flash_sr2 = _read_sr2();
        return true;
    }

    // PIO-based retry
    uint8_t cmd = CMD_JEDEC_ID;
    for (int attempt = 0; attempt < 3; attempt++) {
        g_flash_jedec[0] = g_flash_jedec[1] = g_flash_jedec[2] = 0;
        gpio_put(PIN_ICE_SSn, 0);
        spi_pio_write(&cmd, 1);
        spi_pio_read(g_flash_jedec, 3);
        gpio_put(PIN_ICE_SSn, 1);
        if (g_flash_jedec[0] == FLASH_JEDEC_MANUF &&
            g_flash_jedec[1] == FLASH_JEDEC_TYPE  &&
            g_flash_jedec[2] == FLASH_JEDEC_CAPACITY) {
            g_flash_sr1 = _read_sr1();
            g_flash_sr2 = _read_sr2();
            return true;
        }
        sleep_ms(2);
    }
    return false;
}

void flash_read(uint32_t addr, uint8_t *buf, size_t len) {
    uint8_t cmd = CMD_READ_DATA;
    cs_assert();
    spi_pio_write(&cmd, 1);
    _send_addr(addr);
    spi_pio_read(buf, len);
    cs_deassert();
}

void flash_erase_sector(uint32_t addr) {
    addr &= ~(FLASH_SECTOR_SIZE - 1);
    _write_enable();
    uint8_t cmd = CMD_SECTOR_ERASE;
    cs_assert();
    spi_pio_write(&cmd, 1);
    _send_addr(addr);
    cs_deassert();
    _wait_ready();
}

void flash_erase_block(uint32_t addr) {
    addr &= ~(FLASH_BLOCK_SIZE - 1);
    _write_enable();
    uint8_t cmd = CMD_BLOCK_ERASE;
    cs_assert();
    spi_pio_write(&cmd, 1);
    _send_addr(addr);
    cs_deassert();
    _wait_ready();
}

void flash_write_page(uint32_t addr, const uint8_t *data, size_t len) {
    if (len == 0) return;
    if (len > FLASH_PAGE_SIZE) len = FLASH_PAGE_SIZE;
    _write_enable();
    uint8_t cmd = CMD_PAGE_PROGRAM;
    cs_assert();
    spi_pio_write(&cmd, 1);
    _send_addr(addr);
    spi_pio_write(data, len);
    cs_deassert();
    _wait_ready();
}

void flash_write(uint32_t addr, const uint8_t *data, size_t len) {
    flash_unprotect();

    uint32_t erase_addr = addr & ~(FLASH_SECTOR_SIZE - 1);
    uint32_t erase_end  = (addr + len + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    for (uint32_t a = erase_addr; a < erase_end; a += FLASH_SECTOR_SIZE) {
        flash_erase_sector(a);
    }

    size_t offset = 0;
    while (offset < len) {
        size_t page_len = FLASH_PAGE_SIZE;
        if (offset + page_len > len) page_len = len - offset;
        flash_write_page(addr + offset, data + offset, page_len);
        offset += page_len;
    }
}

bool flash_verify(uint32_t addr, const uint8_t *data, size_t len, uint32_t *fail_addr) {
    uint8_t buf[FLASH_PAGE_SIZE];
    size_t  offset = 0;

    while (offset < len) {
        size_t chunk = FLASH_PAGE_SIZE;
        if (offset + chunk > len) chunk = len - offset;
        flash_read(addr + offset, buf, chunk);
        for (size_t i = 0; i < chunk; i++) {
            if (buf[i] != data[offset + i]) {
                if (fail_addr) *fail_addr = addr + offset + (uint32_t)i;
                return false;
            }
        }
        offset += chunk;
    }
    return true;
}
