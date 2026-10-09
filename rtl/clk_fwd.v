// =============================================================================
//  Takt auf einen Ausgangspin fuehren (ODDR), invertiert:
//  Daten wechseln mit der steigenden internen Flanke = fallende Flanke am Pin,
//  der Empfaenger (ADV7511) tastet mit der steigenden Flanke in der Datenmitte ab.
// =============================================================================
`timescale 1ns / 1ps

module clk_fwd (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 clk CLK" *)
    input  wire clk,
    output wire clk_out
);

    ODDR #(
        .DDR_CLK_EDGE("SAME_EDGE")
    ) oddr (
        .Q (clk_out),
        .C (clk),
        .CE(1'b1),
        .D1(1'b0),
        .D2(1'b1),
        .R (1'b0),
        .S (1'b0)
    );

endmodule
