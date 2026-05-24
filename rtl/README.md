# rtl/ — iCE40UP5K bitstream

Verilog source for the FPGA half of CryptoLite-ICE. The iCE40UP5K is the raw
entropy source: 32 parallel ring oscillators are collapsed into one random bit
per clock and shifted out over a minimal SPI slave to the RP2040. The fabric
runs from a 48 MHz clock supplied by the RP2040 on `clk` (iCE40 pin 44, GBIN5).

All modules follow synchronous-reset-only style: a single `always @(posedge clk)`
per sequential block, active-low `resetn`, no asynchronous sensitivity.

## Source files

| File | Module | Description |
|------|--------|-------------|
| `ro_cell.v` | `ro_cell` | One 8-stage ring oscillator built from `SB_LUT4` primitives (1 buffer + 7 inverters). The odd inversion count guarantees oscillation. `(* keep *)` attributes stop yosys from pruning the feedback loop. |
| `ro_trng.v` | `ro_trng` | The entropy source. Instantiates `n_ro` (default 32) `ro_cell` oscillators and XOR-reduces their free-running outputs, then registers the result on `clk` to cross from the asynchronous oscillator domain into the synchronous fabric. Produces one random bit per clock. |
| `spi_trng_slave.v` | `spi_trng_slave` | Minimal SPI Mode 0 slave. A two-flop synchronizer brings the incoming `sck` into the 48 MHz domain; on every detected falling edge it latches a fresh `rnd_bit` onto `so` (MISO). `SS_B` is intentionally ignored — see the bus-sharing note in the top-level README. |
| `cryptolite_trng_spi.v` | `cryptolite_trng_spi` | Top level. Generates a 16-cycle power-on synchronous reset, instantiates `ro_trng` + `spi_trng_slave`, and drives the on-board RGB LED solid green through the iCE40 `SB_RGBA_DRV` hard IP to signal "TRNG running". |

## Support files

| File | Description |
|------|-------------|
| `cryptolite_trng_spi.pcf` | Pin-constraint file for the iCE40UP5K SG48 package. Required for place & route. |
| `tb_ro_trng.v` | Self-contained iverilog testbench for `ro_trng`. Includes a behavioural `SB_LUT4` stub, so it needs no vendor library. It validates reset behaviour and structure, not statistical quality (in simulation every oscillator uses an identical delay and so toggles in phase). |
| `build_cryptolite_trng_spi.sh` | Synthesis + place & route + packing using the open-source IceStorm flow. |

## Building the bitstream

Requires the open-source IceStorm toolchain (`yosys`, `nextpnr-ice40`,
`icepack`) on `PATH`, or set `ICE40_TOOLCHAIN` to the directory that holds them:

```bash
bash build_cryptolite_trng_spi.sh
# -> build/cryptolite_trng_spi/cryptolite_trng_spi.bin
```

The script passes `--ignore-loops` (the ring-oscillator feedback is an
intentional combinational loop) and `--timing-allow-fail` (the asynchronous
oscillators are not part of the timed design). Program the resulting `.bin`
onto the board with the host tool:

```bash
../host/criptolite-ice.py update-fpga build/cryptolite_trng_spi/cryptolite_trng_spi.bin
```

## Simulating

From this directory, with `iverilog` installed:

```bash
iverilog -o tb_ro_trng.vvp tb_ro_trng.v ro_cell.v ro_trng.v
vvp tb_ro_trng.vvp
```
