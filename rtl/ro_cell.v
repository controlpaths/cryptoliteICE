`default_nettype none
`timescale 1ns / 1ps

/* Single 8-stage ring oscillator for iCE40UP5K.
 *
 * Topology: 1 SB_LUT4 buffer + 7 SB_LUT4 inverters in a closed ring.
 * Seven inversions (odd) guarantee oscillation.
 *
 * INIT values:
 *   16'hAAAA  →  O = I0        (buffer:   INIT[{I3,I2,I1,I0}=0001] = 1, etc.)
 *   16'h5555  →  O = ~I0       (inverter: INIT[{I3,I2,I1,I0}=0000] = 1, etc.)
 *
 * (* keep *) on chain prevents yosys from pruning the feedback wire.
 * SB_LUT4 primitives are black boxes to yosys; they will not be merged.
 */
module ro_cell (
  output wire out
);

  localparam N_STAGES = 8;

  (* keep *) wire [N_STAGES-1:0] chain;

  /* Stage 0 — buffer, closes the ring feedback */
  (* keep *) SB_LUT4 #(.LUT_INIT(16'hAAAA)) u_buf (
    .I0(chain[N_STAGES-1]),
    .I1(1'b0),
    .I2(1'b0),
    .I3(1'b0),
    .O(chain[0])
  );

  /* Stages 1–7 — inverters */
  genvar j;
  generate
    for (j = 1; j < N_STAGES; j = j + 1) begin : gen_inv
      (* keep *) SB_LUT4 #(.LUT_INIT(16'h5555)) u_inv (
        .I0(chain[j-1]),
        .I1(1'b0),
        .I2(1'b0),
        .I3(1'b0),
        .O(chain[j])
      );
    end
  endgenerate

  assign out = chain[N_STAGES-1];

endmodule
