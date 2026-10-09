// Testbench fuer i2s_tx: empfaengt den I2S-Strom wie ein Codec (Abtastung mit
// steigender BCLK-Flanke) und vergleicht mit den per AXI-Stream gelieferten Samples.
`timescale 1ns / 1ps

module i2s_tx_tb;
    reg         clk = 0;
    reg         rstn = 0;
    reg  [23:0] tdata = 0;
    reg         tvalid = 0;
    wire        tready;
    wire        mclk, bclk, lrclk, sdata;

    i2s_tx dut (
        .aclk(clk), .aresetn(rstn),
        .s_axis_tdata(tdata), .s_axis_tvalid(tvalid), .s_axis_tready(tready),
        .i2s_mclk(mclk), .i2s_bclk(bclk), .i2s_lrclk(lrclk), .i2s_sdata(sdata)
    );

    always #40.69 clk = ~clk;                   // ~12,288 MHz

    // --- Quelle: staendig gueltige Samples (wie der HLS-Generator) ---------
    reg [23:0] sent [0:63];
    integer n_sent = 0;
    always @(posedge clk) begin
        if (rstn && tvalid && tready) begin
            sent[n_sent] = tdata;
            n_sent = n_sent + 1;
            tdata <= $random;
        end
    end

    // --- I2S-Empfaenger ----------------------------------------------------
    reg        lr_d = 0;
    reg [31:0] shreg = 0;
    integer    nbits = 0;
    integer    n_left = 0, n_right = 0, errors = 0;
    integer    bclk_per_half = 0;
    reg [23:0] expected;

    always @(posedge bclk) begin
        lr_d <= lrclk;
        if (lrclk !== lr_d) begin               // Kanalwechsel: vorheriges Wort auswerten
            if (n_left + n_right > 1) begin     // erstes (unvollstaendiges) Wort ueberspringen
                if (nbits != 32) begin
                    $display("FEHLER: %0d BCLK pro Kanal statt 32", nbits); errors = errors + 1;
                end
                // shreg enthaelt: [31] 1 Bit Verzoegerung (alt), dann 24 Datenbits, Rest 0
            end
            nbits = 0;
        end
        shreg = {shreg[30:0], sdata};
        nbits = nbits + 1;
        if (nbits == 25) begin                  // 1 Bit I2S-Verzoegerung + 24 Datenbits
            if (lrclk == 1'b0) begin
                if (n_left >= 1) begin
                    // Frame 0 ist Stille nach Reset, Frame k traegt Sample k-1; bei Unterlauf Stille
                    expected = (n_left - 1 < n_sent) ? sent[n_left - 1] : 24'd0;
                    if (shreg[23:0] !== expected) begin
                        $display("FEHLER links  #%0d: %h statt %h", n_left, shreg[23:0], expected);
                        errors = errors + 1;
                    end
                end
                n_left = n_left + 1;
            end else begin
                if (n_right >= 1) begin
                    // Frame 0 ist Stille nach Reset, Frame k traegt Sample k-1; bei Unterlauf Stille
                    expected = (n_right - 1 < n_sent) ? sent[n_right - 1] : 24'd0;
                    if (shreg[23:0] !== expected) begin
                        $display("FEHLER rechts #%0d: %h statt %h", n_right, shreg[23:0], expected);
                        errors = errors + 1;
                    end
                end
                n_right = n_right + 1;
            end
        end
    end

    // LRCLK und SDATA duerfen nur mit fallender BCLK-Flanke wechseln
    always @(lrclk or sdata) if (rstn && $time > 1000 && bclk !== 1'b0) begin
        #1 if (bclk !== 1'b0) begin
            $display("FEHLER: LRCLK/SDATA wechseln nicht mit fallender BCLK-Flanke (t=%0t)", $time);
            errors = errors + 1;
        end
    end

    // Periodendauern pruefen
    realtime t_lr = 0, t_b = 0, p_lr = 0, p_b = 0;
    always @(negedge lrclk) begin p_lr = $realtime - t_lr; t_lr = $realtime; end
    always @(posedge bclk)  begin p_b  = $realtime - t_b;  t_b  = $realtime; end

    initial begin
        repeat (10) @(posedge clk);
        rstn <= 1;
        tdata <= 24'hA5C3F1;
        tvalid <= 1;
        repeat (256 * 20) @(posedge clk);

        // Unterlauf: ohne gueltige Daten muss Stille kommen
        tvalid <= 0;
        repeat (256 * 3) @(posedge clk);
        if (dut.current !== 24'd0) begin
            $display("FEHLER: bei Unterlauf keine Stille"); errors = errors + 1;
        end

        $display("LRCLK-Periode %0.1f ns (soll %0.1f), BCLK-Periode %0.1f ns (soll %0.1f)",
                 p_lr, 256 * 81.38, p_b, 4 * 81.38);
        if (p_lr < 256 * 81.38 * 0.999 || p_lr > 256 * 81.38 * 1.001) errors = errors + 1;
        if (p_b  < 4 * 81.38 * 0.999   || p_b  > 4 * 81.38 * 1.001)   errors = errors + 1;
        $display("Samples: gesendet %0d, links geprueft %0d, rechts geprueft %0d", n_sent, n_left, n_right);
        if (n_left < 15 || n_right < 15) errors = errors + 1;
        if (errors == 0) $display("TEST OK");
        else             $display("TEST FEHLGESCHLAGEN (%0d Fehler)", errors);
        $finish;
    end
endmodule
