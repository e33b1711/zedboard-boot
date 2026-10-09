// =============================================================================
//  I2S-Sender (Master) fuer den ADAU1761 auf dem ZedBoard
//
//  aclk = MCLK = 256 * fs  (12,288 MHz -> fs = 48 kHz)
//  BCLK = MCLK / 4 (64 Bit pro Frame), LRCLK = fs, Standard-I2S, 24 Bit, MSB zuerst
//  Pro Frame wird ein Sample per AXI-Stream abgeholt und auf beiden Kanaelen
//  ausgegeben. Liegt kein Sample an, wird Stille gesendet.
// =============================================================================
`timescale 1ns / 1ps

module i2s_tx #(
    parameter integer DATA_WIDTH = 24
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis, ASSOCIATED_RESET aresetn" *)
    input  wire                  aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire                  aresetn,

    input  wire [DATA_WIDTH-1:0] s_axis_tdata,
    input  wire                  s_axis_tvalid,
    output wire                  s_axis_tready,

    output wire                  i2s_mclk,
    output reg                   i2s_bclk,
    output reg                   i2s_lrclk,
    output reg                   i2s_sdata
);

    // Frame-Zaehler: [1:0] = Phase innerhalb eines BCLK-Takts, [7:2] = Bit-Slot 0..63
    reg  [7:0] cnt;
    wire [7:0] cnt_next = cnt + 8'd1;
    wire [5:0] slot     = cnt_next[7:2];
    wire [4:0] bitpos   = slot[4:0];            // Position innerhalb eines Kanals

    // Ein Sample Vorrat; wird am Frame-Ende uebernommen
    reg                  pending_valid;
    reg [DATA_WIDTH-1:0] pending;
    reg [DATA_WIDTH-1:0] current;

    assign s_axis_tready = ~pending_valid;

    always @(posedge aclk) begin
        if (!aresetn) begin
            cnt           <= 8'd0;
            pending_valid <= 1'b0;
            pending       <= {DATA_WIDTH{1'b0}};
            current       <= {DATA_WIDTH{1'b0}};
            i2s_bclk      <= 1'b0;
            i2s_lrclk     <= 1'b0;
            i2s_sdata     <= 1'b0;
        end else begin
            cnt <= cnt_next;

            if (s_axis_tvalid && s_axis_tready) begin
                pending       <= s_axis_tdata;
                pending_valid <= 1'b1;
            end
            if (cnt == 8'hFF) begin             // Frame-Wechsel
                current       <= pending_valid ? pending : {DATA_WIDTH{1'b0}};
                pending_valid <= 1'b0;
            end

            // Alle Ausgaenge aus demselben Zaehlerstand -> wechseln mit fallender BCLK-Flanke
            i2s_bclk  <= cnt_next[1];
            i2s_lrclk <= cnt_next[7];           // 0 = links, 1 = rechts
            // I2S: MSB einen BCLK-Takt nach dem LRCLK-Wechsel (Slots 1..DATA_WIDTH)
            if (bitpos >= 5'd1 && bitpos <= DATA_WIDTH)
                i2s_sdata <= current[DATA_WIDTH - bitpos];
            else
                i2s_sdata <= 1'b0;
        end
    end

    // MCLK ueber ODDR auf den Pin (Takt nicht ueber normale Logik fuehren)
    ODDR #(
        .DDR_CLK_EDGE("SAME_EDGE")
    ) mclk_oddr (
        .Q (i2s_mclk),
        .C (aclk),
        .CE(1'b1),
        .D1(1'b1),
        .D2(1'b0),
        .R (1'b0),
        .S (1'b0)
    );

endmodule
