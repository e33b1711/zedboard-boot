// =============================================================================
//  Testbench fuer rtl/irq_timer.v
//
//  1. Messungen mit zufaelliger Antwortzeit: jede Laufzeit (LAST) ist genau die Zahl der
//     Takte, in denen irq gesetzt war; Anzahl, Summe, Quadratsumme, Minimum und Maximum
//     stimmen mit der Rechnung der Testbench ueberein
//  2. LIMIT: nach der vorgegebenen Zahl von Messungen kommt kein IRQ mehr, DONE ist gesetzt
//  3. Pause: zwischen Stopp und naechstem IRQ liegen INTERVAL + 2 bis INTERVAL + RAND_MASK + 2 Takte
//  4. clear loescht die Statistik; ACK ohne gesetzten IRQ zaehlt nicht
//  5. Abschalten bei gesetztem IRQ nimmt ihn zurueck, ohne zu zaehlen
// =============================================================================
`timescale 1ns / 1ps

module irq_timer_tb;

    reg         clk = 0, rstn = 0;
    reg  [11:0] awaddr = 0, araddr = 0;
    reg  [31:0] wdata = 0;
    reg         awvalid = 0, wvalid = 0, bready = 0, arvalid = 0, rready = 0;
    wire        awready, wready, bvalid, arready, rvalid;
    wire [31:0] rdata;
    wire        irq;

    always #5 clk = ~clk;

    irq_timer dut (
        .aclk(clk), .aresetn(rstn),
        .s_axi_awaddr(awaddr), .s_axi_awvalid(awvalid), .s_axi_awready(awready),
        .s_axi_wdata(wdata), .s_axi_wstrb(4'hF), .s_axi_wvalid(wvalid), .s_axi_wready(wready),
        .s_axi_bresp(), .s_axi_bvalid(bvalid), .s_axi_bready(bready),
        .s_axi_araddr(araddr), .s_axi_arvalid(arvalid), .s_axi_arready(arready),
        .s_axi_rdata(rdata), .s_axi_rresp(), .s_axi_rvalid(rvalid), .s_axi_rready(rready),
        .irq(irq));

    localparam CTRL = 12'h00, STATUS = 12'h04, INTERVAL = 12'h08, RAND_MASK = 12'h0C, LIMIT = 12'h10,
               COUNT = 12'h14, LAST = 12'h18, MIN = 12'h1C, MAX = 12'h20, SUM_LO = 12'h24, SUM_HI = 12'h28,
               SQ_LO = 12'h2C, SQ_MID = 12'h30, SQ_HI = 12'h34, NOW = 12'h38, CLK_HZ = 12'h3C;

    integer errors = 0;
    task fail(input [8*60:1] msg, input integer a, input integer b);
        begin
            errors = errors + 1;
            if (errors <= 20) $display("FEHLER: %0s (%0d, %0d)", msg, a, b);
        end
    endtask

    // Schreiben; die Adresse liegt wahlweise einige Takte vor den Daten an
    task axi_write(input [11:0] addr, input [31:0] data, input integer skew);
        begin
            @(posedge clk);
            awaddr <= addr; awvalid <= 1;
            repeat (skew) @(posedge clk);
            wdata <= data; wvalid <= 1;
            @(posedge clk);
            while (!(awready && wready)) @(posedge clk);
            awvalid <= 0; wvalid <= 0; bready <= 1;
            @(posedge clk);
            while (!bvalid) @(posedge clk);
            bready <= 0;
        end
    endtask

    reg [31:0] value;
    task axi_read(input [11:0] addr);
        begin
            @(posedge clk);
            araddr <= addr; arvalid <= 1;
            @(posedge clk);
            while (!arready) @(posedge clk);
            arvalid <= 0; rready <= 1;
            @(posedge clk);
            while (!rvalid) @(posedge clk);
            value = rdata;
            rready <= 0;
        end
    endtask

    task expect_reg(input [11:0] addr, input [31:0] want, input [8*60:1] msg);
        begin
            axi_read(addr);
            if (value !== want) fail(msg, value, want);
        end
    endtask

    // Takte mit gesetztem IRQ und Laenge der Pause davor mitzaehlen
    integer high = 0, low = 0, last_high = 0, last_low = 0, cycles = 0;
    reg irq_d = 0;
    always @(posedge clk) begin
        cycles = cycles + 1;
        irq_d <= irq;
        if (irq) high = high + 1;
        else     low = low + 1;
        if (irq && !irq_d) begin last_low = low; high = 1; end
        if (!irq && irq_d) begin last_high = high; low = 1; end
    end

    // Rechnung der Testbench
    integer n = 0, i, delay;
    reg [31:0] ref_min, ref_max;
    reg [63:0] ref_sum;
    reg [95:0] ref_sq;

    task measure(input integer wait_cycles);
        begin
            wait (irq);
            repeat (wait_cycles) @(posedge clk);
            axi_write(STATUS, 32'h1, wait_cycles % 3);
            if (irq) fail("IRQ nach ACK noch gesetzt", n, 0);
            axi_read(LAST);
            if (value !== last_high) fail("LAST ist nicht die Dauer des IRQ", value, last_high);
            n = n + 1;
            ref_sum = ref_sum + last_high;
            ref_sq  = ref_sq + last_high * last_high;
            if (last_high < ref_min) ref_min = last_high;
            if (last_high > ref_max) ref_max = last_high;
        end
    endtask

    task check_stats;
        begin
            expect_reg(COUNT,  n,              "COUNT");
            expect_reg(MIN,    ref_min,        "MIN");
            expect_reg(MAX,    ref_max,        "MAX");
            expect_reg(SUM_LO, ref_sum[31:0],  "SUM_LO");
            expect_reg(SUM_HI, ref_sum[63:32], "SUM_HI");
            expect_reg(SQ_LO,  ref_sq[31:0],   "SQ_LO");
            expect_reg(SQ_MID, ref_sq[63:32],  "SQ_MID");
            expect_reg(SQ_HI,  ref_sq[95:64],  "SQ_HI");
        end
    endtask

    task reset_ref;
        begin
            n = 0; ref_min = 32'hFFFFFFFF; ref_max = 0; ref_sum = 0; ref_sq = 0;
        end
    endtask

    initial begin
        reset_ref;
        repeat (10) @(posedge clk);
        rstn <= 1;

        expect_reg(CLK_HZ, 100000000, "CLK_HZ");
        expect_reg(MIN, 32'hFFFFFFFF, "MIN nach Reset");
        axi_read(NOW); i = value; delay = cycles;
        repeat (37) @(posedge clk);
        axi_read(NOW);
        if (value - i != cycles - delay) fail("NOW zaehlt nicht im Takt", value - i, cycles - delay);

        // 1. und 3.: 200 Messungen, Pause 22 bis 37 Takte
        axi_write(INTERVAL, 20, 0);
        axi_write(RAND_MASK, 15, 0);
        axi_write(CTRL, 32'h1, 0);
        for (i = 0; i < 200; i = i + 1) begin
            delay = i == 50 ? 70000 : $urandom % 40;
            measure(delay);
            if (i > 0 && (last_low < 22 || last_low > 37)) fail("Pause ausserhalb der Vorgabe", last_low, i);
        end
        check_stats;
        if (ref_max < 70000) fail("lange Laufzeit nicht gemessen", ref_max, 0);
        $display("200 Messungen: min %0d, max %0d, Summe %0d Takte", ref_min, ref_max, ref_sum);

        // 4. Loeschen; ACK ohne IRQ zaehlt nicht
        axi_write(CTRL, 32'h0, 0);
        axi_write(CTRL, 32'h2, 0);
        reset_ref;
        axi_write(STATUS, 32'h1, 0);
        check_stats;
        expect_reg(LAST, 0, "LAST nach clear");

        // 2. LIMIT = 5, Pause 0
        axi_write(INTERVAL, 0, 0);
        axi_write(RAND_MASK, 0, 0);
        axi_write(LIMIT, 5, 0);
        axi_write(CTRL, 32'h1, 0);
        for (i = 0; i < 5; i = i + 1) measure(i);
        repeat (200) @(posedge clk);
        if (irq) fail("IRQ nach Erreichen von LIMIT", 0, 0);
        expect_reg(STATUS, 32'h2, "STATUS: DONE erwartet");
        check_stats;

        // clear bei laufender Messung beginnt von vorn
        axi_write(CTRL, 32'h3, 0);
        reset_ref;
        for (i = 0; i < 5; i = i + 1) measure(3);
        expect_reg(STATUS, 32'h2, "STATUS: DONE nach zweitem Lauf");
        check_stats;

        // 5. Abschalten bei gesetztem IRQ
        axi_write(LIMIT, 0, 0);
        axi_write(CTRL, 32'h3, 0);
        reset_ref;
        wait (irq);
        expect_reg(STATUS, 32'h1, "STATUS: IRQ gesetzt");
        axi_write(CTRL, 32'h0, 0);
        if (irq) fail("IRQ nach Abschalten noch gesetzt", 0, 0);
        repeat (100) @(posedge clk);
        if (irq) fail("IRQ trotz enable = 0", 0, 0);
        check_stats;

        if (errors == 0) $display("TEST OK");
        else $display("TEST FEHLGESCHLAGEN: %0d Fehler", errors);
        $finish;
    end

endmodule
