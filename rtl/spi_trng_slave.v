/**
  Author: P. Trujillo
  Date: May26
  Module name: spi_trng_slave
  Description: Minimal SPI slave for the CryptoLite-ICE TRNG. The RP2040 acts
               as SPI master and clocks bytes out of the FPGA; on every falling
               edge of sck the slave latches the next ro_trng bit on so. SS_B
               is intentionally ignored: the RP2040 keeps the external flash
               de-selected (SS_B HIGH) while talking to the FPGA, so there is
               no bus contention. SI is unused on this slave.
  Dependencies: none (instantiated by cryptolite_trng_spi)
  Revision log: 1.0: Module created.
**/

`default_nettype none
`timescale 1ns / 1ps

module spi_trng_slave (
  /* System */
  input  wire clk,         /* 48 MHz fabric clock                          */
  input  wire resetn,      /* synchronous active-low reset                 */

  /* Entropy input */
  input  wire rnd_bit,     /* one fresh random bit per clk cycle           */

  /* SPI bus (slave) */
  input  wire sck,         /* SPI clock from RP2040 (~6.25 MHz)            */
  output wire so           /* MISO — sampled by master on sck rising edge  */
);

  /* ── SCK edge detector ─────────────────────────────────────────────────
   * sck enters from a pad at an unrelated rate (~6.25 MHz) and must be
   * brought into the 48 MHz fabric domain through a two-flop synchronizer
   * before it is safe to detect edges on it.
   */
  reg [2:0] sck_sync;

  always @(posedge clk) begin
    if (!resetn) begin
      sck_sync <= 3'b000;
    end
    else begin
      sck_sync <= {sck_sync[1:0], sck};
    end
  end

  wire sck_falling;
  assign sck_falling = sck_sync[2] & ~sck_sync[1];

  /* ── Output register ───────────────────────────────────────────────────
   * SPI Mode 0: slave shifts new data on the falling edge so the master
   * can sample it on the next rising edge. We capture a fresh rnd_bit on
   * every detected falling edge of sck. While sck is idle, so simply
   * holds the last bit — the master is not clocking, so the value is
   * irrelevant.
   */
  reg so_r;

  always @(posedge clk) begin
    if (!resetn) begin
      so_r <= 1'b0;
    end
    else if (sck_falling) begin
      so_r <= rnd_bit;
    end
  end

  assign so = so_r;

endmodule
