/* =============================================================================
 *  Linien fuer Testbild 8 (HLS-Core pattern_gen): Eintrag der Linientabelle rechnen
 *
 *  Der Core dividiert nicht; die Steigung kommt von hier. Ein Eintrag = 2 Worte zu 64 Bit
 *  (Format siehe hls/pattern_gen.h):
 *    Wort 0  [15:0] x, [31:16] y0, [47:32] y1, [63:48] Flags (Bit 0 gueltig, Bit 1 flach)
 *    Wort 1  [31:0] Steigung, Schritt in x pro Zeile mit 16 Nachkommabits
 *  Gezeichnet werden alle Pixel der Strecke einschliesslich beider Endpunkte.
 *
 *  line_encode()  ganzzahlige Endpunkte auf dem Bildschirm -> Eintrag
 *  line_clip()    Strecke mit beliebigen Koordinaten auf den Bildschirm beschneiden und
 *                 kodieren; liegt sie ganz ausserhalb, wird der Eintrag leer (ungueltig)
 *
 *  Benutzt von vec_demo (Board) und der Testbench des Cores (hls/pattern_gen_tb.cpp).
 * ============================================================================= */
#ifndef TARGET_LINES_H
#define TARGET_LINES_H

#include <stdint.h>
#include <stdlib.h>

#define LINES_W      1920
#define LINES_H      1080
#define LINES_N      32         /* Eintraege pro Puffer */
#define LINES_FRAC   16
#define LINES_VALID  0x1
#define LINES_FLAT   0x2

static inline void line_encode(int xa, int ya, int xb, int yb, uint64_t w[2]) {
    if (ya > yb) { int t = xa; xa = xb; xb = t; t = ya; ya = yb; yb = t; }
    int rows = yb - ya + 1, dx = xb - xa;
    /* die Linie ueberstreicht |dx| + 1 Pixel; zur Null hin abgerundet, damit sie nie
     * ueber ihren Endpunkt hinauslaeuft */
    int32_t step = (int32_t)((((int64_t)abs(dx) + 1) << LINES_FRAC) / rows);
    uint64_t flags = LINES_VALID | (abs(dx) > yb - ya ? LINES_FLAT : 0);
    uint64_t x = (uint64_t)(dx >= 0 ? xa : xa + 1);
    w[0] = (flags << 48) | ((uint64_t)yb << 32) | ((uint64_t)ya << 16) | x;
    w[1] = (uint32_t)(dx >= 0 ? step : -step);
}

/* Liang-Barsky: Strecke auf [0, W-1] x [0, H-1] beschneiden. Rueckgabe 1, wenn etwas
 * sichtbar bleibt. */
static inline int line_clip(double xa, double ya, double xb, double yb, uint64_t w[2]) {
    double t0 = 0, t1 = 1, dx = xb - xa, dy = yb - ya;
    double p[4] = { -dx, dx, -dy, dy };
    double q[4] = { xa, (LINES_W - 1) - xa, ya, (LINES_H - 1) - ya };
    w[0] = w[1] = 0;
    for (int i = 0; i < 4; i++) {
        if (p[i] == 0) {
            if (q[i] < 0) return 0;
        } else {
            double t = q[i] / p[i];
            if (p[i] < 0) { if (t > t1) return 0; if (t > t0) t0 = t; }
            else          { if (t < t0) return 0; if (t < t1) t1 = t; }
        }
    }
    int x0 = (int)(xa + t0 * dx + 0.5), y0 = (int)(ya + t0 * dy + 0.5);
    int x1 = (int)(xa + t1 * dx + 0.5), y1 = (int)(ya + t1 * dy + 0.5);
    #define LINES_CLAMP(v, hi) ((v) < 0 ? 0 : (v) > (hi) ? (hi) : (v))
    line_encode(LINES_CLAMP(x0, LINES_W - 1), LINES_CLAMP(y0, LINES_H - 1),
                LINES_CLAMP(x1, LINES_W - 1), LINES_CLAMP(y1, LINES_H - 1), w);
    #undef LINES_CLAMP
    return 1;
}

#endif
