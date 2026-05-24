# mcu/ — RP2040 firmware

Firmware for the Raspberry Pi RP2040 that sits between the host PC and the
iCE40 FPGA. It is a USB Vendor-class device (no CDC, no mass storage): the host
talks to it with the binary protocol defined in `protocol.h`. The firmware:

1. Clocks raw entropy out of the FPGA over a PIO-based SPI master.
2. Runs continuous SP 800-90B health tests (RCT + APT) on the raw stream.
3. Conditions the entropy with SHA-256 before handing it to the host.
4. Can re-flash the FPGA bitstream over USB by driving the shared SPI bus.

Built against the Raspberry Pi Pico SDK and TinyUSB (Vendor class only).

## Source files

| File | Description |
|------|-------------|
| `main.c` | Entry point. Brings up GPIOs, USB and the FPGA, then runs the main service loop. |
| `protocol.c` / `.h` | Binary command dispatcher over the two bulk endpoints. Defines the request/response framing and the `PROTO_ERR_*` status codes. |
| `trng.c` / `.h` | SPI sampler. Clocks the FPGA slave and collects raw random bits into a buffer. |
| `rng_pipeline.c` / `.h` | Conditioning pipeline: raw bits -> entropy pool -> SHA-256 -> conditioned output bytes. |
| `nist_health.c` / `.h` | Continuous SP 800-90B health tests: Repetition Count Test (RCT) and Adaptive Proportion Test (APT). |
| `sha256.c` / `.h` | SHA-256 implementation used as the conditioning function. |
| `flash_writer.c` / `.h` | USB-driven FPGA bitstream programmer. Holds the FPGA in reset and writes a new bitstream to the configuration flash (U6). |
| `ice40.c` / `.h` | FPGA control: `CRESET_B` reset, clock output enable, `CDONE` sensing, and SPI bus ownership handover. |
| `spi_pio.c` / `.h` / `.pio` | SPI Mode 0 master implemented on the RP2040 PIO (the chosen pins are not on the hardware SPI mux). |
| `spi_flash.c` / `.h` | Driver for the W25Q32 SPI configuration flash. |
| `led_status.c` / `.h` | Two-LED status state machine (BOOT / NO_FPGA / IDLE / BUSY / PROGRAM / ERROR). |
| `usb_descriptors.c` | USB Vendor-class descriptors (VID `0x2E8A`, PID `0x000F`). |
| `tusb_config.h` | TinyUSB configuration — Vendor interface only. |
| `pins.h` | RP2040 GPIO assignments (see the signal table in the top-level README). |

## Build files

| File | Description |
|------|-------------|
| `CMakeLists.txt` | Pico SDK build description. Locates the SDK via `PICO_SDK_PATH` (environment or cache), falling back to `../pico-sdk`. |
| `build_criptolite_rp.sh` | Convenience wrapper: initialises the TinyUSB submodule, runs CMake + make, and prints flashing instructions. |

## Building the firmware

You need `arm-none-eabi-gcc`, CMake (>= 3.13), `make`, and a Pico SDK checkout.
Point `PICO_SDK_PATH` at the SDK, or clone it as `../pico-sdk` at the repo root
(the SDK is **not** vendored in this repository):

```bash
export PICO_SDK_PATH=/path/to/pico-sdk
bash build_criptolite_rp.sh
# -> build/criptolite_rp/criptolite_rp.uf2
```

See the top-level README for first-time BOOTSEL flashing and the full USB
protocol reference.
