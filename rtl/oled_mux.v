// =============================================================================
//  Leitungen der OLED-Anzeige: Takt und Daten wahlweise vom SPI-Core oder direkt
//  aus dem Steuerwort (von Hand getaktet, zur Fehlersuche)
//
//  ctrl: [0] D/C, [1] RES, [2] VBAT, [3] VDD   (RES, VBAT, VDD bei 0 aktiv)
//        [4] 1 = SCLK und SDIN aus [5] und [6], 0 = vom SPI-Core
//        [5] SCLK von Hand
//        [6] SDIN von Hand
// =============================================================================
`timescale 1ns / 1ps

module oled_mux (
    input  wire [6:0] ctrl,
    input  wire       spi_sck,
    input  wire       spi_sdo,
    output wire       oled_sclk,
    output wire       oled_sdin,
    output wire [3:0] oled_gpio
);

    assign oled_sclk = ctrl[4] ? ctrl[5] : spi_sck;
    assign oled_sdin = ctrl[4] ? ctrl[6] : spi_sdo;
    assign oled_gpio = ctrl[3:0];

endmodule
