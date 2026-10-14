// =============================================================================
//  Interrupt am Bildanfang: pattern_gen wechselt bei jeder Uebernahme der Linientabelle
//  den Pegel von frame_tick; jeder Wechsel gibt hier einen Puls von PULSE Takten.
//  Ein Pegelwechsel statt eines Pulses aus dem HLS-Core, weil HLS einen Puls 1-0 in
//  einem Aufruf wegoptimiert. Der Ausgang ist als Interrupt (steigende Flanke)
//  gekennzeichnet, damit er mit dem IRQ von irq_timer an IRQ_F2P gehen kann.
//  Puls bei 148,5 MHz: 64 Takte = 431 ns.
// =============================================================================
`timescale 1ns / 1ps

module frame_irq #(
    parameter integer PULSE = 64
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    input  wire aclk,
    input  wire tick,
    (* X_INTERFACE_INFO = "xilinx.com:signal:interrupt:1.0 irq INTERRUPT" *)
    (* X_INTERFACE_PARAMETER = "SENSITIVITY EDGE_RISING" *)
    output wire irq
);
    reg       tick_d = 1'b0;
    reg [6:0] left = 7'd0;              // Takte, die der Puls noch dauert

    always @(posedge aclk) begin
        tick_d <= tick;
        if (tick != tick_d) left <= PULSE;
        else if (left != 0) left <= left - 1'b1;
    end

    assign irq = left != 0;
endmodule
