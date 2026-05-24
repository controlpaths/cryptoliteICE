/*
 * protocol.h — Binary command protocol over USB Vendor for CryptoLite-RP
 * controlpaths.com | lut7.dev
 *
 * Wire format:
 *
 *   Request  : [cmd:1][rsv:1][payload_len_le:2][payload...]
 *   Response : [cmd:1][status:1][payload_len_le:2][payload...]
 *
 * `cmd` is echoed in the response. `status` is 0 on success or one of
 * the PROTO_ERR_* codes on failure. payload_len is little-endian and
 * counts only the bytes that follow the header (max 65535).
 *
 * Commands are documented inline below.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

/* ── Command IDs ─────────────────────────────────────────────────────── */
#define PROTO_CMD_STATUS            0x01  /* req: -      ; resp: status block */
#define PROTO_CMD_GET_RANDOM        0x02  /* req: u32 N  ; resp: N raw bytes  */
#define PROTO_CMD_FLASH_BEGIN       0x03  /* req: u32 sz ; resp: -            */
#define PROTO_CMD_FLASH_DATA        0x04  /* req: u32 off + bytes ; resp: -   */
#define PROTO_CMD_FLASH_END         0x05  /* req: u32 crc; resp: u8 cdone     */
#define PROTO_CMD_REBOOT_BOOTLOADER 0x06  /* req: -      ; resp: -            */

/* ── Status codes ─────────────────────────────────────────────────────── */
#define PROTO_OK                    0x00
#define PROTO_ERR_BAD_CMD           0x01
#define PROTO_ERR_BAD_LENGTH        0x02
#define PROTO_ERR_BUSY              0x03
#define PROTO_ERR_FLASH             0x04
#define PROTO_ERR_HEALTH_FAIL       0x05
#define PROTO_ERR_HEALTH_WARMING    0x06
#define PROTO_ERR_NO_ENTROPY        0x07
#define PROTO_ERR_INTERNAL          0xFF

void protocol_init(void);
void protocol_task(void);
