// =============================================================================
//  Laufzeit eines Interrupts messen: PL setzt den IRQ, der Rechner setzt ihn zurueck
//
//  Ein frei laufender Zaehler (now, ein Schritt pro Takt) ist die gemeinsame Zeitbasis.
//  Start: das Modul setzt irq und merkt sich den Zaehlerstand. Stopp: der Rechner schreibt
//  ueber AXI-Lite eine 1 in das Bit ACK; in dem Takt, in dem dieser Schreibzugriff
//  ankommt, faellt irq und die Differenz der Zaehlerstaende ist die Laufzeit. Sie ist
//  genau die Zahl der Takte, in denen irq gesetzt war.
//
//  Aus den Laufzeiten fuehrt das Modul Anzahl, Summe, Quadratsumme, Minimum und Maximum.
//  Daraus rechnet der Rechner Mittelwert, Streuung (Jitter) und den schlechtesten Fall.
//
//  Nach jedem Stopp wartet das Modul INTERVAL + (Zufallszahl & RAND_MASK) + 2 Takte bis
//  zum naechsten IRQ. Die Zufallszahl verhindert, dass die Messung mit dem Zeittakt des
//  Betriebssystems im Gleichschritt laeuft.
//
//  Register (32 Bit):
//    0x00 CTRL       [0] enable: Messung laeuft; 0 nimmt einen gesetzten IRQ zurueck,
//                        ohne ihn zu zaehlen
//                    [1] clear (nur schreiben): Statistik, Anzahl und DONE loeschen
//    0x04 STATUS     lesen:     [0] irq gesetzt, [1] DONE: LIMIT erreicht,
//                               [2] eine Laufzeit war laenger als 2^32-1 Takte
//                    schreiben: [0] = 1 ACK: IRQ zuruecksetzen, Stopp der Messung
//    0x08 INTERVAL   Pause zwischen Stopp und naechstem IRQ in Takten
//    0x0C RAND_MASK  Maske fuer den zufaelligen Zuschlag zur Pause
//    0x10 LIMIT      Zahl der Messungen, danach DONE (0 = ohne Ende)
//    0x14 COUNT      Zahl der Messungen
//    0x18 LAST       letzte Laufzeit in Takten
//    0x1C MIN        kleinste Laufzeit (0xFFFFFFFF, solange COUNT = 0)
//    0x20 MAX        groesste Laufzeit
//    0x24 SUM_LO     Summe der Laufzeiten, 64 Bit
//    0x28 SUM_HI
//    0x2C SQ_LO      Summe der Quadrate der Laufzeiten, 96 Bit
//    0x30 SQ_MID
//    0x34 SQ_HI
//    0x38 NOW        Zeitzaehler
//    0x3C CLK_HZ     Takte pro Sekunde
//
//  Die Statistik aendert sich drei Takte nach einem ACK; so lange wartet ein Lesezugriff,
//  ein Lesen direkt nach dem ACK liefert also schon die neue Messung. Wer liest, waehrend
//  ein anderer Prozess quittiert, kann bei den Werten aus mehreren Registern Teile aus
//  zwei Messungen bekommen; sicher ist das Lesen bei enable = 0 oder aus dem Prozess,
//  der quittiert.
// =============================================================================
`timescale 1ns / 1ps

module irq_timer #(
    parameter integer CLK_HZ = 100000000
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axi, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,

    input  wire [11:0] s_axi_awaddr,
    input  wire        s_axi_awvalid,
    output wire        s_axi_awready,
    input  wire [31:0] s_axi_wdata,
    input  wire [3:0]  s_axi_wstrb,
    input  wire        s_axi_wvalid,
    output wire        s_axi_wready,
    output wire [1:0]  s_axi_bresp,
    output reg         s_axi_bvalid,
    input  wire        s_axi_bready,
    input  wire [11:0] s_axi_araddr,
    input  wire        s_axi_arvalid,
    output wire        s_axi_arready,
    output reg  [31:0] s_axi_rdata,
    output wire [1:0]  s_axi_rresp,
    output reg         s_axi_rvalid,
    input  wire        s_axi_rready,

    (* X_INTERFACE_INFO = "xilinx.com:signal:interrupt:1.0 irq INTERRUPT" *)
    (* X_INTERFACE_PARAMETER = "SENSITIVITY LEVEL_HIGH" *)
    output reg         irq
);

    // ---- AXI-Lite: Adresse und Daten werden im selben Takt angenommen ----
    wire stat_busy;                         // eine Laufzeit ist auf dem Weg in die Statistik
    wire wr = s_axi_awvalid && s_axi_wvalid && !s_axi_bvalid;
    wire rd = s_axi_arvalid && !s_axi_rvalid && !stat_busy;
    wire [3:0] wr_reg = s_axi_awaddr[5:2];
    wire [3:0] rd_reg = s_axi_araddr[5:2];
    wire wr_low = wr && s_axi_awaddr[11:6] == 6'd0 && s_axi_wstrb[0];   // Bits 7..0 gueltig

    assign s_axi_awready = wr;
    assign s_axi_wready  = wr;
    assign s_axi_arready = rd;
    assign s_axi_bresp   = 2'b00;
    assign s_axi_rresp   = 2'b00;

    wire wr_ctrl = wr_low && wr_reg == 4'd0;
    wire clear   = wr_ctrl && s_axi_wdata[1];
    wire ack     = wr_low && wr_reg == 4'd1 && s_axi_wdata[0];

    reg        enable;
    reg [31:0] interval, rand_mask, limit;

    integer b;
    always @(posedge aclk)
        if (!aresetn) begin
            s_axi_bvalid <= 1'b0;
            enable    <= 1'b0;
            interval  <= 32'd0;
            rand_mask <= 32'd0;
            limit     <= 32'd0;
        end else begin
            if (wr)                s_axi_bvalid <= 1'b1;
            else if (s_axi_bready) s_axi_bvalid <= 1'b0;

            if (wr_ctrl) enable <= s_axi_wdata[0];
            if (wr && s_axi_awaddr[11:6] == 6'd0)
                for (b = 0; b < 4; b = b + 1)
                    if (s_axi_wstrb[b]) case (wr_reg)
                        4'd2: interval[8*b +: 8]  <= s_axi_wdata[8*b +: 8];
                        4'd3: rand_mask[8*b +: 8] <= s_axi_wdata[8*b +: 8];
                        4'd4: limit[8*b +: 8]     <= s_axi_wdata[8*b +: 8];
                        default: ;
                    endcase
        end

    // ---- Zeitbasis und Zufallszahl ----
    reg [31:0] now;
    reg [31:0] lfsr;
    always @(posedge aclk)
        if (!aresetn) begin
            now  <= 32'd0;
            lfsr <= 32'h1;
        end else begin
            now  <= now + 32'd1;
            lfsr <= {1'b0, lfsr[31:1]} ^ (lfsr[0] ? 32'h80200003 : 32'd0);
        end

    // ---- Ablauf: Pause, IRQ setzen (Start), auf ACK warten (Stopp) ----
    localparam IDLE = 2'd0, PAUSE = 2'd1, PEND = 2'd2;
    reg [1:0]  state;
    reg [31:0] pause, t_start;
    reg        late;                        // Laufzeit hat den Zaehlbereich verlassen
    reg        done, late_seen;
    reg [31:0] count;

    reg        dt_valid;
    reg [31:0] dt;

    wire [31:0] elapsed   = now - t_start;
    wire        last_one  = count == 32'hFFFFFFFE || (limit != 32'd0 && count + 32'd1 >= limit);

    always @(posedge aclk)
        if (!aresetn) begin
            state     <= IDLE;
            irq       <= 1'b0;
            done      <= 1'b0;
            late      <= 1'b0;
            late_seen <= 1'b0;
            dt_valid  <= 1'b0;
        end else begin
            dt_valid <= 1'b0;
            if (clear) begin
                done      <= 1'b0;
                late_seen <= 1'b0;
            end

            case (state)
            IDLE:  if (enable && !done && !clear) begin
                       pause <= interval + (lfsr & rand_mask);
                       state <= PAUSE;
                   end
            PAUSE: if (pause == 32'd0) begin
                       irq     <= 1'b1;
                       t_start <= now;
                       late    <= 1'b0;
                       state   <= PEND;
                   end else
                       pause <= pause - 32'd1;
            PEND:  begin
                       if (&elapsed) late <= 1'b1;
                       if (ack) begin
                           irq      <= 1'b0;
                           dt       <= (late || &elapsed) ? 32'hFFFFFFFF : elapsed;
                           dt_valid <= 1'b1;
                           if (late || &elapsed) late_seen <= 1'b1;
                           if (last_one) done <= 1'b1;
                           state <= IDLE;
                       end
                   end
            default: state <= IDLE;
            endcase

            // Abschalten oder Loeschen bricht ab; ein gesetzter IRQ wird nicht gezaehlt
            if (!enable || clear || (wr_ctrl && !s_axi_wdata[0])) begin
                state    <= IDLE;
                irq      <= 1'b0;
                dt_valid <= 1'b0;
            end
        end

    // ---- Statistik; das Quadrat laeuft ueber zwei Registerstufen (DSP-Bloecke) ----
    reg [1:0]  sq_valid;
    reg [31:0] dt_1, dt_2;
    reg [63:0] sq_1, sq_2;
    reg [31:0] last, min, max;
    reg [63:0] sum;
    reg [95:0] sum_sq;

    assign stat_busy = dt_valid || sq_valid != 2'b00;

    always @(posedge aclk) begin
        dt_1 <= dt;
        dt_2 <= dt_1;
        sq_1 <= dt * dt;
        sq_2 <= sq_1;
    end

    always @(posedge aclk)
        if (!aresetn || clear) begin
            sq_valid <= 2'b00;
            count    <= 32'd0;
            last     <= 32'd0;
            min      <= 32'hFFFFFFFF;
            max      <= 32'd0;
            sum      <= 64'd0;
            sum_sq   <= 96'd0;
        end else begin
            sq_valid <= {sq_valid[0], dt_valid};
            // COUNT zaehlt sofort, damit LIMIT auch bei dicht folgenden Messungen stimmt
            if (dt_valid) count <= count + 32'd1;
            if (sq_valid[1]) begin
                last   <= dt_2;
                sum    <= sum + dt_2;
                sum_sq <= sum_sq + sq_2;
                if (dt_2 < min) min <= dt_2;
                if (dt_2 > max) max <= dt_2;
            end
        end

    // ---- Lesen ----
    reg [31:0] rd_mux;
    always @* begin
        case (rd_reg)
        4'd0:  rd_mux = {31'd0, enable};
        4'd1:  rd_mux = {29'd0, late_seen, done, irq};
        4'd2:  rd_mux = interval;
        4'd3:  rd_mux = rand_mask;
        4'd4:  rd_mux = limit;
        4'd5:  rd_mux = count;
        4'd6:  rd_mux = last;
        4'd7:  rd_mux = min;
        4'd8:  rd_mux = max;
        4'd9:  rd_mux = sum[31:0];
        4'd10: rd_mux = sum[63:32];
        4'd11: rd_mux = sum_sq[31:0];
        4'd12: rd_mux = sum_sq[63:32];
        4'd13: rd_mux = sum_sq[95:64];
        4'd14: rd_mux = now;
        default: rd_mux = CLK_HZ;
        endcase
        if (s_axi_araddr[11:6] != 6'd0) rd_mux = 32'd0;
    end

    always @(posedge aclk)
        if (!aresetn)
            s_axi_rvalid <= 1'b0;
        else if (rd) begin
            s_axi_rvalid <= 1'b1;
            s_axi_rdata  <= rd_mux;
        end else if (s_axi_rready)
            s_axi_rvalid <= 1'b0;

endmodule
