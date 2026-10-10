// =============================================================================
//  Testbench fuer rtl/spec_trace.v
//
//  1. ctrl[23] = 0: I/Q-Daten laufen unveraendert durch
//  2. ctrl[23] = 1: ein bekanntes Spektrum einspeisen und einen Durchlauf pruefen –
//     jede Spalte liefert einen lueckenlosen senkrechten Strich, der alle 4 Linien der
//     Spalte ueberdeckt und an den Strich der Nachbarspalte anschliesst; keine Zelle doppelt
//  3. ctrl[24] = 1: die Max-Hold-Kurve kommt dazu
// =============================================================================
`timescale 1ns / 1ps

module spec_trace_tb;

    reg         clk = 0, rstn = 0;
    reg  [31:0] ctrl = 0;
    reg  [15:0] iq_data = 0, spec_data = 0;
    reg         iq_valid = 0, spec_valid = 0, spec_last = 0;
    wire        iq_ready, spec_ready;
    wire [15:0] xy;
    wire        xy_valid;
    reg         xy_ready = 1;

    always #3.367 clk = ~clk;

    spec_trace #(.PERIOD_BITS(15)) dut (
        .aclk(clk), .aresetn(rstn), .ctrl(ctrl),
        .s_axis_iq_tdata(iq_data), .s_axis_iq_tvalid(iq_valid), .s_axis_iq_tready(iq_ready),
        .s_axis_spec_tdata(spec_data), .s_axis_spec_tvalid(spec_valid), .s_axis_spec_tready(spec_ready),
        .s_axis_spec_tlast(spec_last),
        .m_axis_xy_tdata(xy), .m_axis_xy_tvalid(xy_valid), .m_axis_xy_tready(xy_ready));

    integer errors = 0;
    task fail(input [8*60:1] msg, input integer a, input integer b);
        begin
            errors = errors + 1;
            if (errors <= 20) $display("FEHLER: %0s (%0d, %0d)", msg, a, b);
        end
    endtask

    // Pegel der Linie i (Reihenfolge der FFT): Rauschboden mit Welligkeit, zwei Traeger
    function [7:0] level(input integer i);
        begin
            level = 8'd60 + ((i * 7) % 13);
            if (i == 100) level = 8'd230;
            if (i == 99 || i == 101) level = 8'd214;
            if (i == 824) level = 8'd150;
        end
    endfunction
    function [7:0] held(input integer i);
        held = level(i) + 8'd20;
    endfunction

    // Punkte eines Durchlaufs je Zelle zaehlen
    reg [7:0] hits [0:65535];
    integer points = 0, k, c, j, lo, hi, plo, phi, bin, v, first_y, last_y;
    reg counting = 0;
    always @(posedge clk) begin
        xy_ready <= ($random & 3) != 0;
        if (counting && xy_valid && xy_ready) begin
            hits[{xy[15:8] ^ 8'h80, xy[7:0] ^ 8'h80}] = hits[{xy[15:8] ^ 8'h80, xy[7:0] ^ 8'h80}] + 1;
            points = points + 1;
        end
    end

    task send_spectrum;
        integer i;
        begin
            for (i = 0; i < 1024; i = i + 1) begin
                @(posedge clk);
                spec_data  <= {held(i), level(i)};
                spec_valid <= 1;
                spec_last  <= i == 1023;
            end
            @(posedge clk);
            spec_valid <= 0;
        end
    endtask

    // Einen ganzen Durchlauf mitzaehlen: von einer Pause bis zur naechsten
    task capture;
        begin
            wait (dut.state == 0);
            for (k = 0; k < 65536; k = k + 1) hits[k] = 0;
            points = 0;
            counting = 1;
            wait (dut.state != 0);
            wait (dut.state == 0);
            @(posedge clk);
            counting = 0;
        end
    endtask

    // Erwartung fuer eine Kurve (which = 0 Pegel, 1 Max-Hold) gegen die Treffer pruefen
    task check_trace(input integer which);
        begin
            for (c = 0; c < 256; c = c + 1) begin
                lo = 255; hi = 0;
                for (j = 0; j < 4; j = j + 1) begin
                    bin = ((c * 4 + j) + 512) % 1024;
                    v = which ? held(bin) : level(bin);
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                end
                first_y = lo; last_y = hi;
                if (c > 0 && lo > phi + 1) first_y = phi + 1;
                if (c > 0 && hi < plo - 1) last_y = plo - 1;
                for (k = first_y; k <= last_y; k = k + 1)
                    if (hits[{k[7:0], c[7:0]}] == 0) fail("Zelle der Kurve fehlt", c, k);
                plo = lo; phi = hi;
            end
        end
    endtask

    integer total;
    initial begin
        repeat (10) @(posedge clk);
        rstn <= 1;

        // 1. Durchreichen
        @(posedge clk); iq_data <= 16'h1234; iq_valid <= 1;
        @(negedge clk);
        if (xy !== 16'h1234 || !xy_valid || iq_ready !== xy_ready) fail("I/Q wird nicht durchgereicht", xy, xy_valid);
        @(posedge clk); iq_valid <= 0;

        // 2. Spektrum, nur die Pegelkurve
        send_spectrum;
        ctrl <= 32'h0080_0000;
        iq_valid <= 1;                          // I/Q-Daten muessen jetzt verworfen werden
        capture;
        if (iq_ready !== 1'b1) fail("I/Q-Eingang im Spektrum-Betrieb nicht bereit", 0, 0);
        check_trace(0);
        total = 0;
        for (k = 0; k < 65536; k = k + 1) begin
            if (hits[k] > 1) fail("Zelle doppelt getroffen", k % 256, k / 256);
            total = total + hits[k];
        end
        $display("Pegelkurve: %0d Punkte in %0d Zellen", points, total);
        if (hits[{8'd230, 8'd153}] == 0) fail("Traeger auf Linie 100 fehlt in Spalte 153", 0, 0);
        if (hits[{8'd150, 8'd78}] == 0) fail("Traeger auf Linie 824 fehlt in Spalte 78", 0, 0);
        if (points < 256 || points > 4000) fail("Punktzahl unplausibel", points, 0);

        // 3. mit Max-Hold-Kurve
        ctrl <= 32'h0180_0000;
        capture;
        check_trace(0);
        check_trace(1);
        $display("mit Max-Hold: %0d Punkte", points);

        if (errors == 0) $display("TEST OK");
        else $display("TEST FEHLGESCHLAGEN: %0d Fehler", errors);
        $finish;
    end

endmodule
