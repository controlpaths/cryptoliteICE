/*
 * nist_health.c — NIST SP 800-90B continuous health tests (RCT + APT)
 * controlpaths.com | lut7.dev
 */

#include "nist_health.h"

/* ── State ───────────────────────────────────────────────────────────── */

static nist_health_status_t _status;
static uint32_t _bits_seen;

/* RCT */
static uint8_t  _rct_last;        /* last sample value                  */
static uint32_t _rct_run;         /* current run length                 */
static uint32_t _rct_max;

/* APT */
static uint8_t  _apt_a;           /* reference symbol (window first)    */
static uint32_t _apt_count;       /* occurrences of A in current window */
static uint32_t _apt_pos;         /* position in current window         */
static uint32_t _apt_max;

/* ── Internal helpers ────────────────────────────────────────────────── */

static void _start_apt_window(uint8_t first_bit) {
    _apt_a     = first_bit;
    _apt_count = 1;
    _apt_pos   = 1;
}

/* ── Public API ──────────────────────────────────────────────────────── */

void nist_health_reset(void) {
    _status     = NIST_HEALTH_WARMING;
    _bits_seen  = 0;

    _rct_last   = 0xFF;   /* impossible value → first feed starts a run */
    _rct_run    = 0;
    _rct_max    = 0;

    _apt_a      = 0;
    _apt_count  = 0;
    _apt_pos    = NIST_APT_WINDOW;  /* forces fresh window on first bit */
    _apt_max    = 0;
}

void nist_health_feed_bit(uint8_t bit) {
    bit &= 1u;

    /* Once we've latched a failure, stop updating tests so the user can
     * inspect the offending values via nist_health_get_stats(). */
    if (_status == NIST_HEALTH_FAIL_RCT || _status == NIST_HEALTH_FAIL_APT) {
        return;
    }

    /* ── RCT ───────────────────────────────────────────────────────── */
    if (bit == _rct_last) {
        _rct_run++;
    }
    else {
        _rct_last = bit;
        _rct_run  = 1;
    }
    if (_rct_run > _rct_max) {
        _rct_max = _rct_run;
    }
    if (_rct_run >= NIST_RCT_CUTOFF) {
        _status = NIST_HEALTH_FAIL_RCT;
        return;
    }

    /* ── APT ───────────────────────────────────────────────────────── */
    if (_apt_pos >= NIST_APT_WINDOW) {
        if (_apt_count > _apt_max) {
            _apt_max = _apt_count;
        }
        _start_apt_window(bit);
    }
    else {
        if (bit == _apt_a) {
            _apt_count++;
        }
        _apt_pos++;
        if (_apt_count >= NIST_APT_CUTOFF) {
            _status = NIST_HEALTH_FAIL_APT;
            return;
        }
    }

    /* ── Bookkeeping ───────────────────────────────────────────────── */
    _bits_seen++;
    if (_status == NIST_HEALTH_WARMING && _bits_seen >= NIST_STARTUP_BITS) {
        _status = NIST_HEALTH_OK;
    }
}

void nist_health_feed_byte(uint8_t byte) {
    for (int i = 0; i < 8; i++) {
        nist_health_feed_bit((byte >> i) & 1u);
    }
}

void nist_health_feed(const uint8_t *data, size_t n) {
    for (size_t i = 0; i < n; i++) {
        nist_health_feed_byte(data[i]);
    }
}

nist_health_status_t nist_health_status(void) {
    return _status;
}

bool nist_health_passing(void) {
    return _status == NIST_HEALTH_OK;
}

const char *nist_health_status_str(void) {
    switch (_status) {
        case NIST_HEALTH_WARMING:  return "warming";
        case NIST_HEALTH_OK:       return "ok";
        case NIST_HEALTH_FAIL_RCT: return "FAIL (RCT)";
        case NIST_HEALTH_FAIL_APT: return "FAIL (APT)";
    }
    return "?";
}

void nist_health_get_stats(uint32_t *bits_seen,
                           uint32_t *rct_max,
                           uint32_t *apt_max) {
    if (bits_seen) *bits_seen = _bits_seen;
    if (rct_max)   *rct_max   = _rct_max;
    if (apt_max)   *apt_max   = _apt_max;
}
