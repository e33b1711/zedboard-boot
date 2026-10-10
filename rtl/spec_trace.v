// =============================================================================
//  Spektrum als Kurve fuer das I/Q-Oszilloskop zeichnen
//
//  Nimmt die Spektren des Cores iq_spectrum entgegen (1024 Linien, je Linie
//  tdata[7:0] = Pegel, tdata[15:8] = Max-Hold) und gibt sie als Punktfolge im Format
//  des Oszilloskop-Eingangs aus: x = Frequenz (256 Spalten zu je 4 Linien, Mitte =
//  Mittenfrequenz), y = Pegel. So nutzt das Spektrum Bildspeicher, Nachleuchten und
//  Farbtabellen des Oszilloskops mit.
//
//  Gezeichnet wird in festem Takt (alle 2^PERIOD_BITS Takte ein Durchlauf, bei 148,5 MHz
//  und 21 Bit etwa 71 mal pro Sekunde), unabhaengig davon, wie oft ein neues Spektrum
//  kommt. Je Spalte entsteht ein senkrechter Strich vom kleinsten zum groessten Pegel ihrer
//  4 Linien, verlaengert bis an den Strich der Nachbarspalte: eine geschlossene Kurve, in
//  der jede Zelle pro Durchlauf genau einmal getroffen wird.
//
//  ctrl: [23] 1 = Spektrum zeichnen, 0 = I/Q-Daten unveraendert durchreichen
//        [24] Max-Hold-Kurve zusaetzlich zeichnen
//  Im Spektrum-Betrieb werden die I/Q-Daten an diesem Eingang verworfen (der Eingang
//  bleibt bereit, damit die Datenquelle nicht stockt).
// =============================================================================
`timescale 1ns / 1ps

module spec_trace #(
    parameter integer PERIOD_BITS = 21
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis_iq:s_axis_spec:m_axis_xy, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,

    input  wire [31:0] ctrl,

    input  wire [15:0] s_axis_iq_tdata,
    input  wire        s_axis_iq_tvalid,
    output wire        s_axis_iq_tready,

    input  wire [15:0] s_axis_spec_tdata,
    input  wire        s_axis_spec_tvalid,
    output wire        s_axis_spec_tready,
    input  wire [0:0]  s_axis_spec_tlast,

    output wire [15:0] m_axis_xy_tdata,
    output wire        m_axis_xy_tvalid,
    input  wire        m_axis_xy_tready
);

    wire spectrum = ctrl[23];
    wire show_max = ctrl[24];

    // ---- Speicher fuer das letzte Spektrum ----
    (* ram_style = "block" *) reg [15:0] mem [0:1023];
    reg [9:0]  wr_index;
    reg [9:0]  rd_addr;
    reg [15:0] rd_data;

    integer n;
    initial for (n = 0; n < 1024; n = n + 1) mem[n] = 16'd0;

    assign s_axis_spec_tready = 1'b1;

    always @(posedge aclk) begin
        if (s_axis_spec_tvalid) mem[wr_index] <= s_axis_spec_tdata;
        rd_data <= mem[rd_addr];
    end

    always @(posedge aclk)
        if (!aresetn)                wr_index <= 10'd0;
        else if (s_axis_spec_tvalid) wr_index <= s_axis_spec_tlast ? 10'd0 : wr_index + 10'd1;

    // ---- Zeichnen ----
    localparam IDLE = 3'd0, READ = 3'd1, WAIT = 3'd2, TAKE = 3'd3, RANGE = 3'd4, EMIT = 3'd5;
    reg [2:0] state;
    reg [PERIOD_BITS-1:0] timer;
    reg       pending;                      // ein Durchlauf ist faellig
    reg [7:0] col;                          // Spalte 0..255
    reg [1:0] line;                         // Linie innerhalb der Spalte
    reg       first;                        // erste Spalte: kein Anschluss nach links
    reg       trace;                        // 0 = Pegel, 1 = Max-Hold

    // kleinster und groesster Wert der Spalte und der Spalte davor, je Kurve
    reg [7:0] lo [0:1], hi [0:1], prev_lo [0:1], prev_hi [0:1];
    reg [7:0] y, y_end;

    wire [7:0] level [0:1];
    assign level[0] = rd_data[7:0];
    assign level[1] = rd_data[15:8];

    // Strich der Spalte, bis an den Strich der Spalte davor verlaengert
    function [15:0] range(input [7:0] l, input [7:0] h, input [7:0] pl, input [7:0] ph, input f);
        reg [7:0] from, to;
        begin
            from = l;
            to   = h;
            if (!f && l > ph && l - ph > 8'd1) from = ph + 8'd1;
            if (!f && h < pl && pl - h > 8'd1) to   = pl - 8'd1;
            range = {to, from};
        end
    endfunction

    wire [15:0] range0 = range(lo[0], hi[0], prev_lo[0], prev_hi[0], first);
    wire [15:0] range1 = range(lo[1], hi[1], prev_lo[1], prev_hi[1], first);

    integer t;
    always @(posedge aclk)
        if (!aresetn || !spectrum) begin
            state   <= IDLE;
            timer   <= 0;
            pending <= 1'b0;
        end else begin
            timer <= timer + 1'b1;
            if (&timer) pending <= 1'b1;

            case (state)
            IDLE: if (pending) begin
                      pending <= 1'b0;
                      col   <= 8'd0;
                      line  <= 2'd0;
                      first <= 1'b1;
                      state <= READ;
                  end
            // Linie 512 (unterste Frequenz) steht in Spalte 0, Linie 0 in der Mitte
            READ: begin
                      rd_addr <= {~col[7], col[6:0], line};
                      state   <= WAIT;
                  end
            WAIT: state <= TAKE;
            TAKE: begin
                      for (t = 0; t < 2; t = t + 1) begin
                          if (line == 2'd0 || level[t] < lo[t]) lo[t] <= level[t];
                          if (line == 2'd0 || level[t] > hi[t]) hi[t] <= level[t];
                      end
                      line  <= line + 2'd1;
                      state <= line == 2'd3 ? RANGE : READ;
                  end
            RANGE: begin
                      y     <= range0[7:0];
                      y_end <= range0[15:8];
                      trace <= 1'b0;
                      state <= EMIT;
                  end
            EMIT: if (m_axis_xy_tready) begin
                      if (y != y_end) begin
                          y <= y + 8'd1;
                      end else if (!trace && show_max) begin
                          y     <= range1[7:0];
                          y_end <= range1[15:8];
                          trace <= 1'b1;
                      end else begin
                          for (t = 0; t < 2; t = t + 1) begin
                              prev_lo[t] <= lo[t];
                              prev_hi[t] <= hi[t];
                          end
                          first <= 1'b0;
                          col   <= col + 8'd1;
                          state <= col == 8'd255 ? IDLE : READ;
                      end
                  end
            default: state <= IDLE;
            endcase
        end

    // Das Oszilloskop erwartet I und Q mit Vorzeichen: Spalte und Pegel um 128 verschoben
    wire [15:0] point = {~y[7], y[6:0], ~col[7], col[6:0]};

    assign m_axis_xy_tdata  = spectrum ? point : s_axis_iq_tdata;
    assign m_axis_xy_tvalid = spectrum ? state == EMIT : s_axis_iq_tvalid;
    assign s_axis_iq_tready = spectrum ? 1'b1 : m_axis_xy_tready;

endmodule
