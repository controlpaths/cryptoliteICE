/*
 * trng.c — Raw entropy capture from the iCE40 TRNG (SPI variant)
 * controlpaths.com | lut7.dev
 *
 * The FPGA implements a one-bit SPI slave on (sck, so). On every falling
 * edge of sck it latches a fresh random bit on so. The RP2040 acts as
 * SPI master: clocking out 0x00 bytes and capturing whatever the slave
 * shifts back. SS_B stays HIGH the whole time so the external flash on
 * the same bus stays de-selected.
 */

#include "trng.h"
#include "spi_pio.h"
#include "pins.h"

/* Read chunk size in bytes. Small enough that a single trng_task() call
 * returns within a fraction of a millisecond at SPI_PIO_FREQ_HZ. */
#define TRNG_READ_CHUNK     32u

_Static_assert((TRNG_BUFFER_BYTES & (TRNG_BUFFER_BYTES - 1)) == 0,
               "TRNG_BUFFER_BYTES must be a power of two");

static uint8_t  _ring[TRNG_BUFFER_BYTES];
static volatile uint32_t _head;     /* next write position (bytes) */
static volatile uint32_t _tail;     /* next read  position (bytes) */

static uint32_t _bits_seen;
static uint32_t _overflows;
static bool     _initialised = false;
static bool     _paused      = false;

static inline uint32_t _ring_used(void) {
    return (_head - _tail) & (TRNG_BUFFER_BYTES - 1);
}

static inline uint32_t _ring_free(void) {
    return TRNG_BUFFER_BYTES - 1 - _ring_used();
}

void trng_init(void) {
    _head = 0;
    _tail = 0;
    _bits_seen = 0;
    _overflows = 0;
    _paused = false;
    _initialised = true;
}

void trng_set_paused(bool paused) {
    _paused = paused;
}

bool trng_is_paused(void) {
    return _paused;
}

void trng_task(void) {
    if (!_initialised || _paused) {
        return;
    }

    /* Pull one chunk per call to keep the USB stack responsive. */
    if (_ring_free() < TRNG_READ_CHUNK) {
        return;
    }

    uint8_t buf[TRNG_READ_CHUNK];
    spi_pio_read(buf, TRNG_READ_CHUNK);

    for (uint32_t i = 0; i < TRNG_READ_CHUNK; i++) {
        _ring[_head] = buf[i];
        _head = (_head + 1) & (TRNG_BUFFER_BYTES - 1);
    }
    _bits_seen += TRNG_READ_CHUNK * 8u;
}

size_t trng_available_bytes(void) {
    return _ring_used();
}

size_t trng_read_bytes(uint8_t *dst, size_t n) {
    size_t avail = _ring_used();
    if (n > avail) {
        n = avail;
    }
    for (size_t i = 0; i < n; i++) {
        dst[i] = _ring[_tail];
        _tail = (_tail + 1) & (TRNG_BUFFER_BYTES - 1);
    }
    return n;
}

void trng_get_stats(uint32_t *bits_seen, uint32_t *overflows) {
    if (bits_seen) *bits_seen = _bits_seen;
    if (overflows) *overflows = _overflows;
}
