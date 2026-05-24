`default_nettype none
`timescale 1ns / 1ps

/* ─────────────────────────────────────────────────────────────────────────
 * SB_LUT4 behavioral stub — simulation only.
 *
 * Inertial delay of 1 ns on output allows the ring to oscillate.
 * Initial output set to 0 so all stages start from a known state.
 *
 * In real hardware each RO has slightly different propagation delay due
 * to process variation; here all instances use identical #1 delays, so
 * all ROs oscillate in phase.  rnd_bit toggles periodically rather than
 * appearing random — this testbench validates structure and reset, not
 * statistical entropy.
 * ───────────────────────────────────────────────────────────────────────── */
module SB_LUT4 #(
  parameter [15:0] LUT_INIT = 16'h0000
) (
  input  wire I0,
  input  wire I1,
  input  wire I2,
  input  wire I3,
  output reg  O
);
  initial O = 1'b0;
  always @(I0 or I1 or I2 or I3)
    #1 O = LUT_INIT[{I3, I2, I1, I0}];
endmodule

/* ─────────────────────────────────────────────────────────────────────────
 * tb_ro_trng
 *
 * Simulation command (from the rtl/ directory):
 *   iverilog -o tb_ro_trng.vvp tb_ro_trng.v ro_cell.v ro_trng.v
 *   vvp tb_ro_trng.vvp
 * ───────────────────────────────────────────────────────────────────────── */
module tb_ro_trng;

  localparam CLK_PERIOD = 20;  /* 50 MHz — slow enough to see RO activity */
  localparam N_RO       = 4;   /* small n_ro for fast simulation */
  localparam N_BITS     = 256;

  reg  clk;
  reg  resetn;
  wire rnd_bit;

  ro_trng #(.n_ro(N_RO)) u_dut (
    .clk     (clk),
    .resetn  (resetn),
    .rnd_bit (rnd_bit)
  );

  /* 50 MHz clock */
  initial clk = 1'b0;
  always #(CLK_PERIOD / 2) clk = ~clk;

  /* ── Stimulus ──────────────────────────────────────────────────────────── */
  integer n;
  integer ones;
  integer errors;

  initial begin
    $dumpfile("tb_ro_trng.vcd");
    $dumpvars(0, tb_ro_trng);

    errors  = 0;
    resetn  = 1'b0;

    /* ── Reset check ── */
    repeat (4) @(posedge clk);
    #1;

    if (rnd_bit !== 1'b0) begin
      $display("FAIL [reset] rnd_bit = %b, expected 0", rnd_bit);
      errors = errors + 1;
    end
    else begin
      $display("PASS [reset] rnd_bit = 0 during reset");
    end

    /* ── Release reset and let ROs settle ── */
    @(posedge clk); #1;
    resetn = 1'b1;

    /* One dead cycle while the register absorbs the first sample */
    @(posedge clk); #1;

    /* rnd_bit must no longer be forced to 0 */
    if (rnd_bit === 1'bx) begin
      $display("FAIL [post-reset] rnd_bit is X after reset release");
      errors = errors + 1;
    end
    else begin
      $display("PASS [post-reset] rnd_bit = %b after reset release", rnd_bit);
    end

    /* ── Collect N_BITS samples ── */
    ones = 0;
    for (n = 0; n < N_BITS; n = n + 1) begin
      @(posedge clk); #1;
      if (rnd_bit === 1'b1)
        ones = ones + 1;
    end

    $display("Collected %0d bits — ones: %0d  zeros: %0d",
             N_BITS, ones, N_BITS - ones);

    /* ── Re-assert reset mid-run ── */
    @(posedge clk); #1;
    resetn = 1'b0;
    @(posedge clk); #1;

    if (rnd_bit !== 1'b0) begin
      $display("FAIL [re-reset] rnd_bit = %b, expected 0", rnd_bit);
      errors = errors + 1;
    end
    else begin
      $display("PASS [re-reset] rnd_bit = 0 after re-assertion of reset");
    end

    /* ── Summary ── */
    if (errors == 0)
      $display("ALL CHECKS PASSED");
    else
      $display("%0d CHECK(S) FAILED", errors);

    $finish;
  end

endmodule
