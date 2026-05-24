/*
 * flash_writer.c — Bitstream programming over USB Vendor for CryptoLite-RP
 * controlpaths.com | lut7.dev
 */

#include "flash_writer.h"
#include "spi_flash.h"
#include "ice40.h"
#include "trng.h"
#include "led_status.h"
#include "tusb.h"
#include "pico/time.h"
#include <string.h>

static bool     _in_progress = false;
static uint32_t _total_size  = 0;

bool flash_writer_in_progress(void) {
    return _in_progress;
}

fw_status_t flash_writer_begin(uint32_t total_size) {
    if (_in_progress) {
        return FW_ERR_BUSY;
    }
    if (total_size == 0 || total_size > FLASH_TOTAL_SIZE) {
        return FW_ERR_RANGE;
    }

    /* Stop pulling random bits — the bus is about to belong to flash. */
    trng_set_paused(true);
    led_status_set(LED_STATE_PROGRAM);

    /* Reset the FPGA and reclaim the SPI bus. CRESET is asserted LOW with
     * the 48 MHz clock still running, which is the documented quirk-free
     * sequence (CLAUDE.md §1: "Assert CRESET_B LOW while the 48 MHz clock
     * is still running"). The clock is left running for the rest of the
     * programming session — flash_init/erase don't depend on it. */
    ice40_claim_spi();          /* CRESET LOW + claim PIO + CS HIGH */

    if (!flash_init()) {
        _in_progress = false;
        led_status_set(LED_STATE_ERROR);
        return FW_ERR_FLASH_ID;
    }

    flash_unprotect();

    /* Erase enough sectors to cover the bitstream. */
    uint32_t end = (total_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    for (uint32_t a = 0; a < end; a += FLASH_SECTOR_SIZE) {
        flash_erase_sector(a);
        /* Keep the USB stack alive between sectors (each erase ~45 ms). */
        tud_task();
    }

    _total_size  = total_size;
    _in_progress = true;
    /* Programming and writing keeps the LED blink busy. */
    led_status_set(LED_STATE_BUSY);
    return FW_OK;
}

fw_status_t flash_writer_data(uint32_t offset, const uint8_t *data, size_t len) {
    if (!_in_progress) {
        return FW_ERR_NOT_BEGUN;
    }
    if (offset + len > _total_size) {
        return FW_ERR_RANGE;
    }

    size_t written = 0;
    while (written < len) {
        uint32_t addr = offset + written;
        size_t   page_room = FLASH_PAGE_SIZE - (addr & (FLASH_PAGE_SIZE - 1));
        size_t   chunk     = len - written;
        if (chunk > page_room) chunk = page_room;
        if (chunk > FLASH_PAGE_SIZE) chunk = FLASH_PAGE_SIZE;

        flash_write_page(addr, data + written, chunk);
        written += chunk;
    }
    return FW_OK;
}

fw_status_t flash_writer_end(uint32_t expected_crc32, bool *cdone_out) {
    (void)expected_crc32;   /* CRC verification not implemented in v1 */

    if (!_in_progress) {
        return FW_ERR_NOT_BEGUN;
    }
    _in_progress = false;

    /* Reboot the FPGA: releases SPI, pulses CRESET, restarts clock. */
    bool cdone = ice40_reboot(50);
    if (cdone_out) *cdone_out = cdone;

    if (cdone) {
        ice40_attach_spi_master();
        trng_set_paused(false);
        led_status_set(LED_STATE_IDLE);
    } else {
        led_status_set(LED_STATE_NO_FPGA);
    }
    return FW_OK;
}
