/**
  Author: P. Trujillo
  Date: May26
  Module name: cryptolite_trng_spi
  Description: Top-level for the CryptoLite-ICE TRNG, SPI variant. Replaces
               the APP0/APP1 application bus of cryptolite_trng with a tiny
               SPI slave on the existing SPI pins (SCK, SO). The RP2040 is
               SPI master and reads a continuous stream of random bits out
               of the FPGA; APP0 and APP1 are left unused so they can be
               reassigned in the future. The RGB LED shows board status:
               solid green = TRNG running.
  Dependencies: ro_trng, spi_trng_slave, SB_RGBA_DRV (iCE40 hard IP)
  Revision log: 1.0: Module created.
**/

`default_nettype none
`timescale 1ns / 1ps

module cryptolite_trng_spi (
  /* System */
  input  wire clk,    /* 48 MHz from RP2040 GPIO25 (iCE40 pin 44, GBIN5) */

  /* SPI slave bus to RP2040 (shared with external flash U6) */
  input  wire sck,    /* SPI clock from RP2040 (iCE40 pin 15)            */
  output wire so,     /* MISO — random bits shifted out (iCE40 pin 14)   */

  /* Status RGB (driven through SB_RGBA_DRV hard IP) */
  output wire led_r,
  output wire led_g,
  output wire led_b
);

  /* ─── Synchronous reset generator ──────────────────────────────────────
   * Hold resetn low for the first 16 clk cycles after power-on so every
   * sequential block starts from a known state. Pure synchronous, no
   * async sensitivity → complies with the workspace Verilog rules.
   */
  reg [3:0] reset_cnt;
  reg       resetn;
  initial begin
    reset_cnt = 4'd0;
    resetn    = 1'b0;
  end
  always @(posedge clk) begin
    if (reset_cnt != 4'hF) begin
      reset_cnt <= reset_cnt + 4'd1;
      resetn    <= 1'b0;
    end
    else begin
      resetn    <= 1'b1;
    end
  end

  /* ─── Entropy source ───────────────────────────────────────────────────
   * One random bit per clk cycle (48 Mbit/s raw rate before the SPI slave
   * subsamples it at ~6.25 MHz).
   */
  wire rnd_bit;
  ro_trng #(
    .n_ro(32)
  ) u_ro_trng (
    .clk(clk),
    .resetn(resetn),
    .rnd_bit(rnd_bit)
  );

  /* ─── SPI slave ────────────────────────────────────────────────────────
   * Latches a fresh rnd_bit on every falling edge of sck, presents it on
   * so for the master to sample on the rising edge (SPI Mode 0).
   */
  spi_trng_slave u_spi_slave (
    .clk(clk),
    .resetn(resetn),
    .rnd_bit(rnd_bit),
    .sck(sck),
    .so(so)
  );

  /* ─── Status LED (solid green) ─────────────────────────────────────────
   * Pins 39/40/41 are open-drain dedicated outputs reachable only through
   * SB_RGBA_DRV. Current set to 0b000011 (low) to avoid blinding levels.
   */
  SB_RGBA_DRV #(
    .CURRENT_MODE("0b1"),
    .RGB0_CURRENT("0b000011"),
    .RGB1_CURRENT("0b000011"),
    .RGB2_CURRENT("0b000011")
  ) u_rgba_driver (
    .CURREN(1'b1),
    .RGBLEDEN(1'b1),
    .RGB0PWM(1'b0),  /* R off */
    .RGB1PWM(1'b1),  /* G on  */
    .RGB2PWM(1'b0),  /* B off */
    .RGB0(led_r),
    .RGB1(led_g),
    .RGB2(led_b)
  );

endmodule
