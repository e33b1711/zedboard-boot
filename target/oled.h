/* =============================================================================
 *  OLED-Anzeige des ZedBoards (128 x 32 Pixel, Controller SSD1306) ansteuern
 *
 *  Takt und Daten gehen ueber den Core AXI Quad SPI (oled_spi), die uebrigen Leitungen
 *  ueber ein AXI-GPIO mit 7 Ausgaengen (oled_ctrl, dahinter rtl/oled_mux.v):
 *      Bit 0  D/C    0 = Befehl, 1 = Bilddaten
 *      Bit 1  RES    0 = Reset
 *      Bit 2  VBAT   0 = Versorgung der Anzeige ein
 *      Bit 3  VDD    0 = Versorgung der Logik ein
 *      Bit 4..6      Takt und Daten von Hand statt vom SPI-Core (nur zur Fehlersuche, hier 0)
 *  Beide Cores werden ueber /dev/mem bedient; der SPI-Knoten ist im Device Tree
 *  abgeschaltet (system-user.dtsi), damit der Kernel-Treiber ihn nicht belegt.
 *
 *  Das Bild liegt im Controller: nach oled_text() bleibt es stehen, auch wenn das
 *  Programm endet. Text: 2 Zeilen zu 16 Zeichen, 8 x 16 Pixel je Zeichen.
 *
 *  Wird von oled.c und irq_test.c eingebunden.
 * ============================================================================= */
#ifndef OLED_H
#define OLED_H

#include <fcntl.h>
#include <glob.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "oled_font.h"

/* Register des AXI Quad SPI, als Index in 32-Bit-Worten */
#define SPI_SRR   (0x40 / 4)        /* 0x0A schreiben: Core zuruecksetzen */
#define SPI_CR    (0x60 / 4)
#define SPI_SR    (0x64 / 4)
#define SPI_DTR   (0x68 / 4)
#define SPI_DRR   (0x6C / 4)
#define SPI_SSR   (0x70 / 4)
#define SPI_CR_ENABLE   0x086       /* SPE, Master, Slave Select von Hand; Modus 0, MSB zuerst */
#define SPI_CR_FIFO_RST 0x060
#define SPI_SR_RX_EMPTY 0x01
#define SPI_SR_TX_FULL  0x08

#define OLED_DC   1
#define OLED_RES  2
#define OLED_VBAT 4
#define OLED_VDD  8
#define OLED_ON   OLED_RES          /* beide Versorgungen ein, kein Reset */

#define OLED_COLS 128
#define OLED_PAGES 4                /* je 8 Pixelzeilen */

static volatile uint32_t *oled_spi, *oled_gpio;
static unsigned oled_sent, oled_got;    /* gesendete Bytes und ihre Rueckmeldungen */

/* Adresse aus der Eigenschaft reg eines Knotens; mit width nur Knoten dieser GPIO-Breite */
static int oled_node(const char *pattern, uint32_t width, uint32_t *addr) {
    glob_t g;
    int found = -1;
    if (glob(pattern, 0, NULL, &g) != 0) return -1;
    for (size_t i = 0; i < g.gl_pathc && found; i++) {
        char path[300];
        uint8_t b[4];
        FILE *f;
        if (width) {
            snprintf(path, sizeof path, "%s/xlnx,gpio-width", g.gl_pathv[i]);
            f = fopen(path, "rb");
            int ok = f && fread(b, 1, 4, f) == 4 && b[3] == width && !b[0] && !b[1] && !b[2];
            if (f) fclose(f);
            if (!ok) continue;
        }
        snprintf(path, sizeof path, "%s/reg", g.gl_pathv[i]);
        f = fopen(path, "rb");
        if (f && fread(b, 1, 4, f) == 4) {
            *addr = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
            found = 0;
        }
        if (f) fclose(f);
    }
    globfree(&g);
    return found;
}

static void oled_sleep_ms(int ms) {
    struct timespec ts = { ms / 1000, (ms % 1000) * 1000000L };
    nanosleep(&ts, NULL);
}

static void oled_drain(void) {
    while (!(oled_spi[SPI_SR] & SPI_SR_RX_EMPTY)) {
        (void)oled_spi[SPI_DRR];
        oled_got++;
    }
}

static void oled_byte(uint8_t b) {
    while (oled_spi[SPI_SR] & SPI_SR_TX_FULL) oled_drain();
    oled_spi[SPI_DTR] = b;
    oled_sent++;
    oled_drain();
}

/* Warten, bis alle Bytes auf der Leitung waren: zu jedem kommt eines zurueck */
static int oled_flush(void) {
    for (int i = 0; i < 2000 && oled_got != oled_sent; i++) {
        oled_drain();
        if (oled_got != oled_sent && i >= 1000) oled_sleep_ms(1);
    }
    if (oled_got == oled_sent) return 0;
    fprintf(stderr, "FEHLER: OLED: der SPI-Core sendet nicht (%u von %u Bytes)\n", oled_got, oled_sent);
    oled_got = oled_sent;
    return -1;
}

/* D/C darf erst wechseln, wenn die Bytes davor gesendet sind */
static int oled_send(int data, const uint8_t *bytes, int n) {
    if (oled_flush()) return -1;
    *oled_gpio = (*oled_gpio & ~OLED_DC) | (data ? OLED_DC : 0);
    for (int i = 0; i < n; i++) oled_byte(bytes[i]);
    return oled_flush();
}

/* Cores suchen und einblenden. Rueckgabe -1, wenn der Bitstream keine Anzeige hat. */
static int oled_open(void) {
    uint32_t spi_addr, gpio_addr;
    if (oled_node("/proc/device-tree/*/axi_quad_spi@*", 0, &spi_addr) ||
        oled_node("/proc/device-tree/*/gpio@4*", 7, &gpio_addr)) {
        fprintf(stderr, "FEHLER: Cores der OLED-Anzeige nicht im Device Tree (Bitstream ohne OLED?)\n");
        return -1;
    }
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); return -1; }
    oled_spi = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, spi_addr);
    oled_gpio = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, gpio_addr);
    close(fd);
    if (oled_spi == MAP_FAILED || oled_gpio == MAP_FAILED) { perror("mmap"); return -1; }

    /* Den Core nur einrichten, wenn er es noch nicht ist: die Anzeige hat keinen Chip
     * Select, eine einzige Flanke auf dem Takt beim Zuruecksetzen wuerde alle folgenden
     * Bytes um ein Bit verschieben. */
    if (oled_spi[SPI_CR] != SPI_CR_ENABLE) {
        oled_spi[SPI_SRR] = 0x0A;
        oled_spi[SPI_CR] = SPI_CR_ENABLE | SPI_CR_FIFO_RST;
        oled_spi[SPI_SSR] = 0xFFFFFFFF;
        oled_spi[SPI_CR] = SPI_CR_ENABLE;
    }
    oled_drain();
    oled_sent = oled_got = 0;
    return 0;
}

/* Anzeige einschalten und einrichten; flip dreht das Bild um 180 Grad.
 * Jeder Aufruf schaltet die Anzeige erst ganz aus (etwa 1 s dunkel): das bringt auch ihren
 * Bitzaehler wieder in Tritt, falls zuvor ein Byte nicht vollstaendig ankam.
 * Folge und Befehle wie im ZedBoard-Beispiel von Digilent. Der Controller bleibt dabei auf
 * 64 Zeilen stehen: mit dem ueblichen Befehl fuer 32 Zeilen (A8 1F) blieb die Anzeige auf
 * dem Board dunkel. */
static int oled_init(int flip) {
    static const uint8_t off[] = { 0xAE }, pump[] = { 0x8D, 0x14, 0xD9, 0xF1 }, on[] = { 0xAF };
    const uint8_t setup[] = {
        0x81, 0x0F,                     /* Kontrast */
        flip ? 0xA1 : 0xA0,             /* Spalten von links oder von rechts */
        flip ? 0xC8 : 0xC0,             /* Zeilen von oben oder von unten */
        0xDA, 0x20,                     /* Zuordnung der Zeilenleitungen */
        0x20, 0x00,                     /* Bilddaten laufen spaltenweise durch alle Seiten */
        0xA4,                           /* Bild aus dem Speicher */
    };
    const int pause = 50;               /* ms; so auf dem Board geprueft */

    if (oled_flush()) return -1;
    *oled_gpio = OLED_RES | OLED_VBAT | OLED_VDD;       /* alles aus */
    oled_sleep_ms(pause * 10);
    *oled_gpio = OLED_RES | OLED_VBAT;                  /* Logik ein */
    oled_sleep_ms(pause);
    if (oled_send(0, off, sizeof off)) return -1;
    *oled_gpio = OLED_VBAT;                             /* Reset */
    oled_sleep_ms(pause);
    *oled_gpio = OLED_RES | OLED_VBAT;
    oled_sleep_ms(pause);
    if (oled_send(0, pump, sizeof pump)) return -1;     /* Ladungspumpe */
    *oled_gpio = OLED_ON;                               /* Versorgung der Anzeige ein */
    oled_sleep_ms(200);
    if (oled_send(0, setup, sizeof setup)) return -1;
    return oled_send(0, on, sizeof on);
}

/* Zwei Zeilen Text anzeigen (NULL = leere Zeile); mehr als 16 Zeichen werden abgeschnitten */
static int oled_text(const char *line0, const char *line1) {
    /* Der Controller rechnet mit 64 Zeilen (siehe oled_init): das Bild in beide Haelften */
    static const uint8_t window[] = { 0x21, 0, OLED_COLS - 1, 0x22, 0, 2 * OLED_PAGES - 1 };
    uint8_t image[OLED_PAGES * OLED_COLS] = { 0 };      /* je Byte 8 Pixel untereinander, Bit 0 oben */
    const char *lines[2] = { line0, line1 };

    for (int l = 0; l < 2; l++)
        for (int pos = 0; lines[l] && lines[l][pos] && pos < OLED_COLS / 8; pos++) {
            int ch = (unsigned char)lines[l][pos];
            if (ch < 0x20 || ch > 0x7E) ch = '?';
            for (int row = 0; row < 16; row++)
                for (int col = 0; col < 8; col++)
                    if (oled_font[ch - 0x20][row] & (0x80 >> col))
                        image[(l * 2 + row / 8) * OLED_COLS + pos * 8 + col] |= 1 << (row % 8);
        }
    if (oled_send(0, window, sizeof window)) return -1;
    if (oled_send(1, image, sizeof image)) return -1;
    return oled_send(1, image, sizeof image);
}

/* Anzeige abschalten: erst die Anzeige, dann die Logik */
static void oled_off(void) {
    static const uint8_t off[] = { 0xAE };
    oled_send(0, off, sizeof off);
    *oled_gpio = OLED_RES | OLED_VBAT;
    oled_sleep_ms(100);
    *oled_gpio = OLED_RES | OLED_VBAT | OLED_VDD;
}

#endif
