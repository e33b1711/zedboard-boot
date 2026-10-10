// =============================================================================
//  I/Q-Oszilloskop als Einblendung im Videostrom (1920x1080, YCbCr 4:2:2)
//
//  Bildspeicher: 256 x 256 Zellen zu 16 Bit im Block-RAM. Jeder Abtastwert (I, Q je
//  8 Bit mit Vorzeichen) trifft genau eine Zelle und erhoeht sie um "Schritt".
//  Anzeige: jede Zelle 4 x 4 Pixel gross, Fenster 1024 x 1024 ab (WIN_X0, WIN_Y0);
//  I nach rechts, Q nach oben. Ausserhalb des Fensters laeuft das Bild unveraendert durch.
//  Nachleuchten: einmal pro Bild wird jede Zelle um 1/2^k verkleinert, und zwar beim
//  Auslesen ueber dieselbe Speicherseite, die auch anzeigt.
//
//  s_axis_iq_tdata: [7:0] = I, [15:8] = Q
//  ctrl: [0]     Einblendung ein
//        [1]     eingebautes Testsignal statt s_axis_iq
//        [2]     Testsignal: 0 = Kreis, 1 = Lissajous-Figur (Frequenzen etwa 1:2)
//        [3]     Hintergrund durchscheinend (sonst schwarzes Fenster)
//        [7:4]   k, Nachleuchten ueber etwa 2^k Bilder
//        [15:8]  Schritt pro Treffer
//        [19:16] Helligkeit: angezeigt wird Zellwert >> Wert, begrenzt auf 255
//        [21:20] Farbtabelle (siehe gen_palette.py)
//        [22]    Achsenkreuz und Rahmen
//        [24:23] gehoeren spec_trace.v (Spektrum statt I/Q-Daten am Eingang)
// =============================================================================
`timescale 1ns / 1ps

module iq_scope #(
    parameter integer WIN_X0 = 448,
    parameter integer WIN_Y0 = 28
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 aclk CLK" *)
    (* X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF s_axis_video:m_axis_video:s_axis_iq, ASSOCIATED_RESET aresetn" *)
    input  wire        aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 aresetn RST" *)
    (* X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        aresetn,

    input  wire [31:0] ctrl,

    input  wire [15:0] s_axis_video_tdata,
    input  wire        s_axis_video_tvalid,
    output wire        s_axis_video_tready,
    input  wire [0:0]  s_axis_video_tuser,      // erstes Pixel des Bildes
    input  wire [0:0]  s_axis_video_tlast,      // letztes Pixel der Zeile

    output reg  [15:0] m_axis_video_tdata,
    output reg         m_axis_video_tvalid,
    input  wire        m_axis_video_tready,
    output reg  [0:0]  m_axis_video_tuser,
    output reg  [0:0]  m_axis_video_tlast,

    input  wire [15:0] s_axis_iq_tdata,
    input  wire        s_axis_iq_tvalid,
    output wire        s_axis_iq_tready
);

    wire       on      = ctrl[0];
    wire       test    = ctrl[1];
    wire       liss    = ctrl[2];
    wire       transp  = ctrl[3];
    wire [3:0] decay_k = ctrl[7:4];
    wire [7:0] step    = ctrl[15:8];
    wire [3:0] shift   = ctrl[19:16];
    wire [1:0] palette = ctrl[21:20];
    wire       grid    = ctrl[22];

    // ---- Bildspeicher: zwei unabhaengige Seiten, Ausgang bleibt beim Schreiben stehen ----
    (* ram_style = "block" *) reg [15:0] mem [0:65535];
    reg         a_en, a_we;
    reg  [15:0] a_addr, a_din, a_dout;
    wire        b_en, b_we;
    wire [15:0] b_addr, b_din;
    reg  [15:0] b_dout;

    integer n;
    initial for (n = 0; n < 65536; n = n + 1) mem[n] = 16'd0;

    always @(posedge aclk)
        if (a_en) begin
            if (a_we) mem[a_addr] <= a_din;
            else      a_dout <= mem[a_addr];
        end

    always @(posedge aclk)
        if (b_en) begin
            if (b_we) mem[b_addr] <= b_din;
            else      b_dout <= mem[b_addr];
        end

    // ---- Anzeige: Ort des Pixels, das gerade am Eingang anliegt (x, y) ----
    // Alle Stufen ruecken gemeinsam um einen Schritt weiter, und zwar nur, wenn ein neues
    // Pixel ankommt und der Ausgang frei ist. So liegen zwischen zwei Pixeln nie leere
    // Stufen, und die Speicherzugriffe behalten ihren festen Abstand in Schritten.
    // Die letzten Pixel bleiben dafuer stehen, bis die Quelle weiterliefert.
    wire out_free = !m_axis_video_tvalid || m_axis_video_tready;
    wire ce = out_free && s_axis_video_tvalid;
    assign s_axis_video_tready = out_free;
    wire s_fire = ce;

    reg  [11:0] x_cnt, y_cnt;
    wire [11:0] x = s_axis_video_tuser ? 12'd0 : x_cnt;
    wire [11:0] y = s_axis_video_tuser ? 12'd0 : y_cnt;

    always @(posedge aclk)
        if (!aresetn) begin
            x_cnt <= 12'd0;
            y_cnt <= 12'd0;
        end else if (s_fire) begin
            x_cnt <= s_axis_video_tlast ? 12'd0 : x + 12'd1;
            y_cnt <= s_axis_video_tlast ? y + 12'd1 : y;
        end

    // Stufe 0: Pixel mit seinem Ort. Stufe A: Lage im Fenster, Zelle und Speicherzugriff
    // ausgerechnet. Der Zugriff selbst geschieht beim Weiterruecken von Stufe A nach 1;
    // so haengt am Speicher nur Logik, die aus Registern kommt.
    reg        p0_valid, p0_user, p0_last;
    reg [15:0] p0_data;
    reg [11:0] p0_x, p0_y;
    reg        pa_valid, pa_user, pa_last, pa_win, pa_grid, pa_odd, pa_read, pa_fade;
    reg [15:0] pa_data, pa_addr;

    wire [11:0] wx = p0_x - WIN_X0;
    wire [11:0] wy = p0_y - WIN_Y0;
    wire in_win = p0_x >= WIN_X0 && wx < 12'd1024 && p0_y >= WIN_Y0 && wy < 12'd1024;
    wire [15:0] cell_addr = {~wy[9:2], wx[9:2]};         // oberste Zeile = groesstes Q

    // Jede Zelle wird in jeder ihrer 4 Zeilen am ersten Pixel gelesen. In der letzten Zeile
    // wird sie verkleinert zurueckgeschrieben, und zwar 5 Schritte nach dem Lesen: so lange
    // braucht der Wert durch die Register b_val, b_val2, b_val3 und b_new. Die vielen
    // Register sind noetig, weil der Speicher aus 32 Block-RAMs besteht, die weit verteilt
    // liegen; kein Pfad soll vom Speicher durch Logik zurueck in den Speicher fuehren.
    // Der Schreibzugriff faellt immer auf das zweite Pixel der naechsten Zelle und
    // kommt dem Lesen (erstes Pixel) deshalb nie in die Quere.
    wire b_read  = in_win && wx[1:0] == 2'd0;
    wire b_fade  = b_read && wy[1:0] == 2'd3;
    reg  [15:0] b_val, b_val2, b_val3, b_new, fade_r;
    wire [15:0] fade = b_val2 >> decay_k;

    reg  [4:0]  wr_valid;               // Schreibauftrag unterwegs, [4] = im naechsten Schritt
    reg  [15:0] wr_addr [0:4];

    always @(posedge aclk)
        if (ce) begin
            b_val  <= b_dout;
            b_val2 <= b_val;
            b_val3 <= b_val2;
            fade_r <= (fade == 16'd0 && b_val2 != 16'd0) ? 16'd1 : fade;
            b_new  <= b_val3 - fade_r;

            wr_addr[0] <= pa_addr;
            wr_addr[1] <= wr_addr[0];
            wr_addr[2] <= wr_addr[1];
            wr_addr[3] <= wr_addr[2];
            wr_addr[4] <= wr_addr[3];
        end

    always @(posedge aclk)
        if (!aresetn)  wr_valid <= 5'd0;
        else if (ce)   wr_valid <= {wr_valid[3:0], pa_valid && pa_fade};

    wire b_do_read  = pa_valid && pa_read;
    wire b_do_write = wr_valid[4];
    assign b_en   = ce && (b_do_read || b_do_write);
    assign b_we   = b_do_write;
    assign b_addr = b_do_write ? wr_addr[4] : pa_addr;
    assign b_din  = b_new;

    wire on_grid = wx[9:1] == 9'd255 || wy[9:1] == 9'd255       // Achsen durch die Mitte
                || wx[9:1] == 9'd0 || wx[9:1] == 9'd511 || wy[9:1] == 9'd0 || wy[9:1] == 9'd511;

    // Stufe 1: Zellwert liegt am Speicherausgang
    reg        p1_valid, p1_user, p1_last, p1_win, p1_grid, p1_odd;
    reg [15:0] p1_data;
    // Stufe 2: Zellwert liegt in b_val
    reg        p2_valid, p2_user, p2_last, p2_win, p2_grid, p2_odd;
    reg [15:0] p2_data;
    // Stufe V: Zellwert liegt in b_val2
    reg        pv_valid, pv_user, pv_last, pv_win, pv_grid, pv_odd;
    reg [15:0] pv_data;
    // Stufe L: Helligkeit aus dem Zellwert gerechnet
    reg        pl_valid, pl_user, pl_last, pl_win, pl_grid, pl_odd;
    reg [15:0] pl_data;
    reg [7:0]  pl_level;
    // Stufe 3: Farbe liegt am Ausgang der Farbtabelle
    reg        p3_valid, p3_user, p3_last, p3_win, p3_grid, p3_odd, p3_dark;
    reg [15:0] p3_data;

    wire [15:0] bright  = b_val2 >> shift;
    wire [7:0]  level   = |bright[15:8] ? 8'd255 : bright[7:0];
    wire [23:0] color;                                          // {Y, Cb, Cr}

    iq_palette palette_rom (.clk(aclk), .ce(ce), .addr({palette, pl_level}), .q(color));

    wire [15:0] scope_px = p3_odd ? {color[7:0], color[23:16]} : {color[15:8], color[23:16]};
    wire [15:0] empty_px = (grid && p3_grid) ? {8'd128, 8'd80}
                         : transp            ? p3_data
                         :                     {8'd128, 8'd16};

    always @(posedge aclk)
        if (!aresetn) begin
            p0_valid <= 1'b0;
            pa_valid <= 1'b0;
            p1_valid <= 1'b0;
            p2_valid <= 1'b0;
            pv_valid <= 1'b0;
            pl_valid <= 1'b0;
            p3_valid <= 1'b0;
            m_axis_video_tvalid <= 1'b0;
        end else if (ce) begin
            p0_valid <= s_axis_video_tvalid;
            pa_valid <= p0_valid;
            p1_valid <= pa_valid;
            p2_valid <= p1_valid;
            pv_valid <= p2_valid;
            pl_valid <= pv_valid;
            p3_valid <= pl_valid;
            m_axis_video_tvalid <= p3_valid;
        end else if (m_axis_video_tready) begin
            m_axis_video_tvalid <= 1'b0;
        end

    always @(posedge aclk)
        if (ce) begin
            p0_data <= s_axis_video_tdata;
            p0_user <= s_axis_video_tuser;
            p0_last <= s_axis_video_tlast;
            p0_x    <= x;
            p0_y    <= y;

            pa_data  <= p0_data;
            pa_user  <= p0_user;
            pa_last  <= p0_last;
            pa_win   <= in_win;
            pa_grid  <= on_grid;
            pa_odd   <= p0_x[0];
            pa_addr  <= cell_addr;
            pa_read  <= b_read;
            pa_fade  <= b_fade;

            p1_data <= pa_data;
            p1_user <= pa_user;
            p1_last <= pa_last;
            p1_win  <= pa_win;
            p1_grid <= pa_grid;
            p1_odd  <= pa_odd;

            p2_data <= p1_data;
            p2_user <= p1_user;
            p2_last <= p1_last;
            p2_win  <= p1_win;
            p2_grid <= p1_grid;
            p2_odd  <= p1_odd;

            pv_data <= p2_data;
            pv_user <= p2_user;
            pv_last <= p2_last;
            pv_win  <= p2_win;
            pv_grid <= p2_grid;
            pv_odd  <= p2_odd;

            pl_data  <= pv_data;
            pl_user  <= pv_user;
            pl_last  <= pv_last;
            pl_win   <= pv_win;
            pl_grid  <= pv_grid;
            pl_odd   <= pv_odd;
            pl_level <= level;

            p3_data <= pl_data;
            p3_user <= pl_user;
            p3_last <= pl_last;
            p3_win  <= pl_win;
            p3_grid <= pl_grid;
            p3_odd  <= pl_odd;
            p3_dark <= pl_level == 8'd0;

            m_axis_video_tdata <= !(on && p3_win) ? p3_data : p3_dark ? empty_px : scope_px;
            m_axis_video_tuser <= p3_user;
            m_axis_video_tlast <= p3_last;
        end

    // ---- Testsignal: zwei Oszillatoren aus je zwei Addierern, ein Wert alle 8 Takte ----
    reg signed [17:0] c1, s1, c2, s2;
    reg        [2:0]  tick;
    reg               test_valid;
    wire signed [17:0] c1_next = c1 - (s1 >>> 10);
    wire signed [17:0] c2_next = c2 - (s2 >>> 9);
    wire               sample_taken;

    always @(posedge aclk)
        if (!aresetn) begin
            c1 <= 18'sd25600;                   // Amplitude 100
            s1 <= 18'sd0;
            c2 <= 18'sd25600;
            s2 <= 18'sd0;
            tick <= 3'd0;
            test_valid <= 1'b0;
        end else begin
            tick <= tick + 3'd1;
            if (tick == 3'd7) begin
                c1 <= c1_next;
                s1 <= s1 + (c1_next >>> 10);
                c2 <= c2_next;
                s2 <= s2 + (c2_next >>> 9);
                test_valid <= 1'b1;
            end else if (sample_taken) begin
                test_valid <= 1'b0;
            end
        end

    wire [15:0] test_iq = {liss ? s2[15:8] : s1[15:8], c1[15:8]};

    // ---- Abtastwerte eintragen: Zelle lesen, erhoehen, zurueckschreiben ----
    // (Zellwert und Summe laufen ueber Register: 6 Takte pro Abtastwert, also bis 24 Msps)
    localparam IDLE = 3'd0, READ = 3'd1, FETCH = 3'd2, FETCH2 = 3'd3, ADD = 3'd4, WRITE = 3'd5;
    reg  [2:0]  state;
    reg  [15:0] hit, a_val, a_val2, a_new;
    wire        src_valid = test ? test_valid : s_axis_iq_tvalid;
    wire [15:0] src_iq    = test ? test_iq : s_axis_iq_tdata;
    assign sample_taken     = state == IDLE && src_valid;
    assign s_axis_iq_tready = state == IDLE && !test;

    // Greift die Anzeige in diesem Takt vielleicht auf dieselbe Zelle zu, wartet diese Seite
    wire busy = (pa_read && pa_addr == hit) || (wr_valid[4] && wr_addr[4] == hit);
    wire [16:0] sum = {1'b0, a_val2} + {9'd0, step};

    always @(*) begin
        a_en   = 1'b0;
        a_we   = 1'b0;
        a_addr = hit;
        a_din  = a_new;
        if (state == READ && !busy) a_en = 1'b1;
        if (state == WRITE && !busy) begin
            a_en = 1'b1;
            a_we = 1'b1;
        end
    end

    always @(posedge aclk)
        if (!aresetn) begin
            state <= IDLE;
        end else case (state)
            IDLE:    if (src_valid) begin
                         hit   <= {~src_iq[15], src_iq[14:8], ~src_iq[7], src_iq[6:0]};
                         state <= READ;
                     end
            READ:    if (!busy) state <= FETCH;
            FETCH:   begin a_val <= a_dout; state <= FETCH2; end
            FETCH2:  begin a_val2 <= a_val; state <= ADD; end
            ADD:     begin a_new <= sum[16] ? 16'hFFFF : sum[15:0]; state <= WRITE; end
            WRITE:   if (!busy) state <= IDLE;
            default: state <= IDLE;
        endcase

endmodule
