/*
 * rng_pipeline.h — TRNG output pipeline for CryptoLite-ICE
 * controlpaths.com | lut7.dev
 *
 * Glues together:
 *   trng         (raw bit capture from FPGA)
 *   nist_health  (continuous SP 800-90B RCT + APT)
 *   sha256       (SP 800-90B vetted conditioning function)
 *
 * The conditioning ratio is fixed: 128 raw bits → SHA-256 → 256 conditioned
 * bits. This keeps an effective entropy density well above 0.5 even with
 * a conservative input min-entropy estimate.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define RNG_BLOCK_RAW_BITS    128u
#define RNG_BLOCK_RAW_BYTES   (RNG_BLOCK_RAW_BITS / 8u)   /* 16 */
#define RNG_BLOCK_OUT_BYTES   32u                          /* SHA-256 digest */

typedef enum {
    RNG_OK = 0,
    RNG_ERR_NOT_ENOUGH_ENTROPY,   /* not enough bits buffered yet         */
    RNG_ERR_HEALTH_FAIL,          /* sticky NIST RCT/APT failure          */
    RNG_ERR_HEALTH_WARMING,       /* startup test not yet complete        */
} rng_status_t;

void rng_pipeline_init(void);

/* Drain raw entropy from the TRNG ring buffer into the health tests so
 * that the RCT/APT state is always up to date. Cheap; call from the main
 * loop after trng_task(). */
void rng_pipeline_task(void);

/* Produce exactly RNG_BLOCK_OUT_BYTES (32) bytes of conditioned random
 * data into out[]. Returns RNG_OK on success or an error code. On
 * RNG_ERR_NOT_ENOUGH_ENTROPY the caller can retry after pumping more
 * trng_task()/rng_pipeline_task() cycles. */
rng_status_t rng_pipeline_get_block(uint8_t out[RNG_BLOCK_OUT_BYTES]);

/* Produce n bytes by chaining as many SHA-256 blocks as needed. Returns
 * the number of bytes actually written; on health failure returns 0. */
size_t rng_pipeline_get_bytes(uint8_t *out, size_t n, rng_status_t *status);
