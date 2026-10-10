// =============================================================================
//  Testbench fuer rtl/iq_scope.v
//
//  Bild 0:    Einblendung aus – der Videostrom muss unveraendert durchlaufen
//  Bild 1, 2: Testsignal (Kreis) – ausserhalb des Fensters unveraendert, im Fenster
//             ein Kreis mit Radius 100 Zellen; Bild 2 wird nach bild_kreis.txt geschrieben
//  Bild 3, 4: einzelne Abtastwerte ueber s_axis_iq – die getroffenen Zellen muessen an
//             der richtigen Stelle leuchten und von Bild zu Bild dunkler werden;
//             Bild 4 wird nach bild_punkte.txt geschrieben
//  Quelle und Senke bremsen zufaellig. Die Dateien enthalten je Pixel ein Wort (hex).
// =============================================================================
`timescale 1ns / 1ps

module iq_scope_tb;

    localparam W = 1920, H = 1080, X0 = 448, Y0 = 28;

    reg         clk = 0, rstn = 0;
    reg  [31:0] ctrl = 0;
    reg  [15:0] s_data;
    reg         s_valid = 0;
    wire        s_ready;
    reg         s_user, s_last;
    wire [15:0] m_data;
    wire        m_valid;
    reg         m_ready = 0;
    wire        m_user, m_last;
    reg  [15:0] iq_data = 0;
    reg         iq_valid = 0;
    wire        iq_ready;

    always #3.367 clk = ~clk;

    iq_scope #(.WIN_X0(X0), .WIN_Y0(Y0)) dut (
        .aclk(clk), .aresetn(rstn), .ctrl(ctrl),
        .s_axis_video_tdata(s_data), .s_axis_video_tvalid(s_valid), .s_axis_video_tready(s_ready),
        .s_axis_video_tuser(s_user), .s_axis_video_tlast(s_last),
        .m_axis_video_tdata(m_data), .m_axis_video_tvalid(m_valid), .m_axis_video_tready(m_ready),
        .m_axis_video_tuser(m_user), .m_axis_video_tlast(m_last),
        .s_axis_iq_tdata(iq_data), .s_axis_iq_tvalid(iq_valid), .s_axis_iq_tready(iq_ready));

    function [15:0] pixel(input integer x, input integer y);
        pixel = (x * 16'd257) ^ (y * 16'd1031) ^ 16'h5AA5;
    endfunction

    integer errors = 0;
    task fail(input [8*60:1] msg, input integer a, input integer b);
        begin
            errors = errors + 1;
            if (errors <= 20) $display("FEHLER: %0s (%0d, %0d)", msg, a, b);
        end
    endtask

    // ---- Quelle: Bilder mit bekanntem Inhalt, zufaellige Pausen ----
    integer sx = 0, sy = 0, frames_sent = 0;
    always @(posedge clk) begin
        if (!rstn) begin
            s_valid <= 0;
        end else begin
            if (s_valid && s_ready) begin
                s_valid <= 0;
                if (sx == W - 1) begin
                    sx = 0;
                    if (sy == H - 1) begin sy = 0; frames_sent = frames_sent + 1; end
                    else sy = sy + 1;
                end else sx = sx + 1;
            end
            if ((!s_valid || s_ready) && ($random & 15) != 0) begin
                s_valid <= 1;
                s_data  <= pixel(sx, sy);
                s_user  <= sx == 0 && sy == 0;
                s_last  <= sx == W - 1;
            end
        end
        m_ready <= ($random & 7) != 0;
    end

    // ---- Senke: Ort mitzaehlen, pruefen, Bilder ablegen ----
    integer mx = 0, my = 0, frame = -1;
    integer lit = 0, ring_ok = 0, ring_bad = 0, file_kreis, file_punkte;
    integer cx, cy, r2;
    reg [7:0] dot_y3, dot_y4;
    wire in_win = mx >= X0 && mx < X0 + 1024 && my >= Y0 && my < Y0 + 1024;

    initial begin
        file_kreis  = $fopen("bild_kreis.txt", "w");
        file_punkte = $fopen("bild_punkte.txt", "w");
    end

    always @(posedge clk) if (rstn && m_valid && m_ready) begin
        if (m_user) begin
            if (frame >= 0 && (mx != 0 || my != H)) fail("Bildanfang an falscher Stelle", mx, my);
            mx = 0; my = 0; frame = frame + 1;
        end
        if (m_last != (mx == W - 1)) fail("Zeilenende an falscher Stelle", mx, my);

        if ((frame == 0 || !in_win) && m_data !== pixel(mx, my)) fail("Pixel veraendert", mx, my);

        if (frame == 2) $fdisplay(file_kreis, "%04x", m_data);
        if (frame == 4) $fdisplay(file_punkte, "%04x", m_data);

        // Kreis: helle Pixel muessen 94..102 Zellen von der Mitte entfernt sein (der einfache
        // Oszillator des Testsignals liefert eine leichte Ellipse, Radius 95 bis 101)
        if (frame == 2 && in_win && m_data[7:0] > 8'd120) begin
            cx = (mx - X0) / 4 - 128;
            cy = 127 - (my - Y0) / 4;
            r2 = cx * cx + cy * cy;
            lit = lit + 1;
            if (r2 >= 94 * 94 && r2 <= 102 * 102) ring_ok = ring_ok + 1;
            else if (!(cx == 0 || cx == -1 || cy == 0 || cy == -1 || cx == -128 || cx == 127 || cy == -128 || cy == 127))
                ring_bad = ring_bad + 1;
        end
        // Punkt bei I = 50, Q = -30: Zelle (178, 98), also Pixel X0 + 712, Y0 + (255 - 98) * 4
        if (mx == X0 + 713 && my == Y0 + 629) begin
            if (frame == 3) dot_y3 = m_data[7:0];
            if (frame == 4) dot_y4 = m_data[7:0];
        end
        if (frame == 4) begin
            // Ecken: I = -128, Q = 127 oben links; I = 127, Q = -128 unten rechts
            if (mx == X0 + 1 && my == Y0 + 1 && m_data[7:0] < 8'd60) fail("Ecke oben links dunkel", m_data, 0);
            if (mx == X0 + 1022 && my == Y0 + 1022 && m_data[7:0] < 8'd60) fail("Ecke unten rechts dunkel", m_data, 0);
            // Nachbarzelle des Punkts bleibt leer: durchscheinender Hintergrund
            if (mx == X0 + 717 && my == Y0 + 629 && m_data !== pixel(mx, my)) fail("Nachbarzelle nicht leer", m_data, 0);
        end

        if (m_last) begin mx = 0; my = my + 1; end else mx = mx + 1;
    end

    task send_iq(input integer i, input integer q, input integer count);
        integer k;
        begin
            for (k = 0; k < count; k = k + 1) begin
                @(posedge clk);
                iq_data  <= {q[7:0], i[7:0]};
                iq_valid <= 1;
                @(posedge clk);
                while (!iq_ready) @(posedge clk);
                iq_valid <= 0;
            end
        end
    endtask

    initial begin
        repeat (20) @(posedge clk);
        rstn <= 1;
        wait (frames_sent == 1);
        // ein, Testsignal Kreis, Achsenkreuz, k = 3, Schritt 1
        ctrl <= 32'h0040_0133;
        wait (frames_sent == 3);
        // ein, durchscheinend, Achsenkreuz, k = 3, Schritt 16, Helligkeit >> 2
        ctrl <= 32'h0042_1039;
        send_iq(50, -30, 20);
        send_iq(-128, 127, 20);
        send_iq(127, -128, 20);
        wait (frame == 5);
        repeat (10) @(posedge clk);

        $display("Kreis: %0d helle Pixel, davon %0d auf dem Ring, %0d daneben", lit, ring_ok, ring_bad);
        if (ring_ok < 8000) fail("Kreis fehlt oder ist unvollstaendig", ring_ok, 0);
        if (ring_bad != 0) fail("helle Pixel neben dem Kreis", ring_bad, 0);
        $display("Punkt: Helligkeit %0d in Bild 3, %0d in Bild 4", dot_y3, dot_y4);
        if (dot_y3 < 8'd60) fail("Punkt fehlt", dot_y3, 0);
        if (!(dot_y4 < dot_y3 && dot_y4 > 8'd40)) fail("Punkt klingt nicht richtig ab", dot_y3, dot_y4);

        $fclose(file_kreis);
        $fclose(file_punkte);
        if (errors == 0) $display("TEST OK");
        else $display("TEST FEHLGESCHLAGEN: %0d Fehler", errors);
        $finish;
    end

endmodule
