/*
 * sha256.h — Tiny portable SHA-256 (FIPS 180-4)
 * controlpaths.com | lut7.dev
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

#define SHA256_DIGEST_BYTES  32
#define SHA256_BLOCK_BYTES   64

typedef struct {
    uint32_t state[8];
    uint64_t bit_len;
    uint8_t  buf[SHA256_BLOCK_BYTES];
    size_t   buf_len;
} sha256_ctx_t;

void sha256_init  (sha256_ctx_t *ctx);
void sha256_update(sha256_ctx_t *ctx, const uint8_t *data, size_t len);
void sha256_final (sha256_ctx_t *ctx, uint8_t out[SHA256_DIGEST_BYTES]);

/* One-shot helper. */
void sha256       (const uint8_t *data, size_t len,
                   uint8_t out[SHA256_DIGEST_BYTES]);
