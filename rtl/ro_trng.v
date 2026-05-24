`default_nettype none
`timescale 1ns / 1ps

/* TRNG entropy source: n_ro parallel ring oscillators.
 *
 * Each ro_cell is an 8-stage LUT ring (7 inverters + 1 buffer).
 * Process variation causes each RO to run at a slightly different
 * frequency; the XOR of all outputs collapses that jitter into
 * a single bit with high toggle rate.
 *
 * rnd_bit is registered on clk to cross from the async RO domain
 * to the synchronous system clock domain.
 *
 * Ports:
 *   clk      48 MHz system clock from RP2040 GPIO25 / GBIN5 (pin 44)
 *   resetn   synchronous active-low reset
 *   rnd_bit  one random bit per clock cycle
 */
module ro_trng #(
  parameter n_ro = 32
) (
  input  wire clk,
  input  wire resetn,
  output reg  rnd_bit
);

  wire [n_ro-1:0] ro_out;

  genvar i;
  generate
    for (i = 0; i < n_ro; i = i + 1) begin : gen_ro
      ro_cell u_ro (
        .out(ro_out[i])
      );
    end
  endgenerate

  always @(posedge clk) begin
    if (!resetn) begin
      rnd_bit <= 1'b0;
    end
    else begin
      rnd_bit <= ^ro_out; /* reduction XOR across all RO outputs */
    end
  end

endmodule
