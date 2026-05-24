/*
 * nist_health.h — NIST SP 800-90B continuous health tests
 * controlpaths.com | lut7.dev
 *
 * Implements the two mandatory health tests on the raw bit stream from
 * the FPGA TRNG:
 *
 *   - Repetition Count Test  (RCT, §4.4.1)
 *   - Adaptive Proportion Test (APT, §4.4.2)
 *
 * Cutoffs are tuned for binary (1-bit) samples with a min-entropy claim
 * of H ≈ 0.65 bits/bit and a per-test false-positive rate α = 2⁻²⁰.
 * They are sized so that a healthy XOR-of-32-RO source never trips them
 * but a stuck/biased source does within a window or two.
 *
 * The tests update a sticky failure state. nist_health_reset() must be
 * called explicitly to clear it (e.g. on user request after diagnosis).
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ── Tunable parameters ───────────────────────────────────────────────── */
#define NIST_RCT_CUTOFF      32u    /* max consecutive identical bits     */
#define NIST_APT_WINDOW     512u    /* samples per APT window             */
#define NIST_APT_CUTOFF     410u    /* max occurrences of A in window     */
#define NIST_STARTUP_BITS  1024u    /* samples before the source is "hot" */

typedef enum {
    NIST_HEALTH_WARMING = 0,    /* still gathering startup samples        */
    NIST_HEALTH_OK,             /* both tests passing                     */
    NIST_HEALTH_FAIL_RCT,       /* RCT cutoff exceeded                    */
    NIST_HEALTH_FAIL_APT,       /* APT cutoff exceeded                    */
} nist_health_status_t;

void nist_health_reset(void);

/* Feed one bit (LSB) to the health tests. */
void nist_health_feed_bit(uint8_t bit);

/* Convenience: feed 8 bits LSB-first. */
void nist_health_feed_byte(uint8_t byte);

/* Convenience: feed n bytes LSB-first per byte. */
void nist_health_feed(const uint8_t *data, size_t n);

nist_health_status_t nist_health_status(void);
bool                 nist_health_passing(void);  /* status == OK */
const char          *nist_health_status_str(void);

/* Diagnostics:
 *   bits_seen — total bits fed since reset
 *   rct_max   — largest run of identical bits seen so far
 *   apt_max   — largest A-count observed in any completed APT window
 */
void nist_health_get_stats(uint32_t *bits_seen,
                           uint32_t *rct_max,
                           uint32_t *apt_max);
