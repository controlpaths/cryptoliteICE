/*
 * rng_pipeline.c — TRNG output pipeline for CryptoLite-RP
 * controlpaths.com | lut7.dev
 *
 * The pipeline drains the raw TRNG ring (filled by trng_task() in the main
 * loop) into a small local pool. As bytes enter the pool they are fed to
 * the NIST SP 800-90B RCT/APT health tests; conditioning consumes from the
 * pool and emits SHA-256(raw[16]) → 32 conditioned bytes.
 */

#include "rng_pipeline.h"
#include "trng.h"
#include "nist_health.h"
#include "sha256.h"

#include "tusb.h"
#include "pico/time.h"

#include <string.h>

void rng_pipeline_init(void) {
    trng_init();
    nist_health_reset();
}

static void _refill_pool(void);

void rng_pipeline_task(void) {
    /* Drain the TRNG ring into the local pool (which also feeds the NIST
     * health tests). Without this, the pool only fills when a request is
     * in flight and the health tests never warm up between calls. */
    _refill_pool();
}

/* ── Internal entropy pool ──────────────────────────────────────────── */
#define POOL_BYTES   256u
static uint8_t  _pool[POOL_BYTES];
static uint32_t _pool_head;
static uint32_t _pool_tail;

static inline uint32_t _pool_used(void) {
    return (_pool_head - _pool_tail) & (POOL_BYTES - 1);
}

static inline uint32_t _pool_free(void) {
    return POOL_BYTES - 1 - _pool_used();
}

_Static_assert((POOL_BYTES & (POOL_BYTES - 1)) == 0,
               "POOL_BYTES must be a power of two");

static void _refill_pool(void) {
    uint8_t buf[32];
    while (_pool_free() >= sizeof(buf)) {
        size_t got = trng_read_bytes(buf, sizeof(buf));
        if (got == 0) {
            return;
        }
        nist_health_feed(buf, got);
        for (size_t i = 0; i < got; i++) {
            _pool[_pool_head] = buf[i];
            _pool_head = (_pool_head + 1) & (POOL_BYTES - 1);
        }
    }
}

static size_t _pool_take(uint8_t *dst, size_t n) {
    size_t avail = _pool_used();
    if (n > avail) {
        n = avail;
    }
    for (size_t i = 0; i < n; i++) {
        dst[i] = _pool[_pool_tail];
        _pool_tail = (_pool_tail + 1) & (POOL_BYTES - 1);
    }
    return n;
}

/* Wait up to timeout_ms while pumping USB and the TRNG, until at least
 * `need` bytes are available in the pool. */
static bool _wait_for_pool(size_t need, uint32_t timeout_ms) {
    uint32_t t0 = to_ms_since_boot(get_absolute_time());
    while (_pool_used() < need) {
        tud_task();
        trng_task();
        _refill_pool();
        if (_pool_used() >= need) break;
        if (to_ms_since_boot(get_absolute_time()) - t0 > timeout_ms) {
            return false;
        }
    }
    return true;
}

/* ── Public ──────────────────────────────────────────────────────── */

rng_status_t rng_pipeline_get_block(uint8_t out[RNG_BLOCK_OUT_BYTES]) {
    if (!_wait_for_pool(RNG_BLOCK_RAW_BYTES, 2000)) {
        return RNG_ERR_NOT_ENOUGH_ENTROPY;
    }

    nist_health_status_t hs = nist_health_status();
    if (hs == NIST_HEALTH_FAIL_RCT || hs == NIST_HEALTH_FAIL_APT) {
        return RNG_ERR_HEALTH_FAIL;
    }
    if (hs == NIST_HEALTH_WARMING) {
        return RNG_ERR_HEALTH_WARMING;
    }

    uint8_t raw[RNG_BLOCK_RAW_BYTES];
    _pool_take(raw, RNG_BLOCK_RAW_BYTES);

    sha256(raw, sizeof(raw), out);
    return RNG_OK;
}

size_t rng_pipeline_get_bytes(uint8_t *out, size_t n, rng_status_t *status) {
    size_t written = 0;
    rng_status_t st = RNG_OK;

    while (written < n) {
        uint8_t block[RNG_BLOCK_OUT_BYTES];
        st = rng_pipeline_get_block(block);
        if (st != RNG_OK) {
            break;
        }
        size_t take = n - written;
        if (take > sizeof(block)) {
            take = sizeof(block);
        }
        memcpy(out + written, block, take);
        written += take;
    }

    if (status) *status = st;
    return written;
}
