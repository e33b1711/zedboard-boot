// C-Testbench: rendert jedes Testbild, prueft Bildformat, Rahmenmarken und Stichproben
// und schreibt die Bilder als PPM (zurueckgewandelt nach RGB) ins Arbeitsverzeichnis.
// Die Framebuffer liegen in einem Speichermodell; geprueft wird, dass sie in der
// Reihenfolge 0, 1, 2 und unveraendert ausgegeben werden.
// Das Linienbild wird Pixel fuer Pixel mit einem Modell verglichen und zusaetzlich
// geometrisch gegen die Endpunkte geprueft, fuer das feste Bild und fuer Linien aus der
// Tabelle (beide Puffer, Umschalten, Bildzaehler, Beschneiden am Bildrand).
// Mit -DCOSIM (kleines Bild, siehe build_hls.tcl) laufen nur die Framebuffer-Tests.
#include "pattern_gen.h"
#include "lines_rom.h"
#include "../target/lines.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <vector>

struct frame_t { std::vector<unsigned char> y, c; int errors; };

// Speichermodell: die drei Framebuffer liegen absichtlich nicht in der Reihenfolge 0, 1, 2
static ap_uint<64> mem[FB_MEM_WORDS];
static const unsigned FB_ADDR[FB_COUNT] = {0, 2 * FRAME_BYTES, 1 * FRAME_BYTES};

// Byte i des Framebuffers n: fuer jeden Puffer und jede Stelle ein anderer Wert
static unsigned char fb_byte(int n, unsigned i) {
    unsigned h = (i + 1) * 2654435761u + n * 40503u;
    return (h >> 13) ^ (h >> 21) ^ n;
}

static void fill_fb() {
    for (int n = 0; n < FB_COUNT; n++)
        for (unsigned w = 0; w < FRAME_WORDS; w++) {
            ap_uint<64> v = 0;
            for (int b = 0; b < 8; b++) v(8 * b + 7, 8 * b) = fb_byte(n, 8 * w + b);
            mem[FB_ADDR[n] / 8 + w] = v;
        }
}

// Linientabelle im Core, Register lines_ctrl und die Rueckmeldungen des letzten Bildes
static ap_uint<64> lines_mem[2 * LINE_BUF_WORDS];
static int lines_ctrl = 0;
static unsigned last_frames = 0, last_cur = 0;

// Ein Aufruf des Cores = ein Bild
static frame_t render(int pattern, unsigned color, int format = 0, int fb_frames = 0) {
    frame_t f; f.errors = 0;
    hls::stream<video_t> s;
    ap_uint<2> cur; ap_uint<32> frames; ap_uint<1> tick;
    pattern_gen(pattern, color, format, FB_ADDR[0], FB_ADDR[1], FB_ADDR[2], fb_frames, mem,
                lines_mem, lines_ctrl, cur, frames, tick, s);
    if (frames != last_frames + 1) { std::printf("FEHLER: Bildzaehler %u nach %u\n", frames.to_uint(), last_frames); f.errors++; }
    if (tick != (frames & 1)) { std::printf("FEHLER: frame_tick passt nicht zum Bildzaehler\n"); f.errors++; }
    last_frames = frames; last_cur = cur;
    for (int yy = 0; yy < FRAME_HEIGHT; yy++) {
        for (int xx = 0; xx < FRAME_WIDTH; xx++) {
            video_t v = s.read();
            if ((v.user == 1) != (xx == 0 && yy == 0))     f.errors++;   // Start of Frame
            if ((v.last == 1) != (xx == FRAME_WIDTH - 1))  f.errors++;   // End of Line
            f.y.push_back(v.data.to_uint() & 0xFF);
            f.c.push_back(v.data.to_uint() >> 8);
        }
    }
    if (!s.empty()) { std::printf("FEHLER: mehr als ein Bild ausgegeben\n"); f.errors++; }
    return f;
}

// Welcher Framebuffer steckt im Bild? -1 = keiner (auch bei einem einzigen falschen Byte)
// swap_c: Cb und Cr sind paarweise vertauscht, swap_yc: Y und C sind vertauscht
static int which_fb(const frame_t &f, bool swap_c = false, bool swap_yc = false) {
    for (int n = 0; n < FB_COUNT; n++) {
        bool same = true;
        for (unsigned px = 0; px < FRAME_WIDTH * FRAME_HEIGHT && same; px++) {
            unsigned char ey = fb_byte(n, 2 * px), ec = fb_byte(n, 2 * (swap_c ? px ^ 1 : px) + 1);
            if (swap_yc) same = (f.y[px] == ec && f.c[px] == ey);
            else         same = (f.y[px] == ey && f.c[px] == ec);
        }
        if (same) return n;
    }
    return -1;
}

static int expect_fb(const frame_t &f, const char *what, int n, bool swap_c = false, bool swap_yc = false) {
    int got = which_fb(f, swap_c, swap_yc);
    if (got != n) std::printf("FEHLER %s: Framebuffer %d statt %d\n", what, got, n);
    return (got != n) + f.errors;
}

// Reihenfolge und Wechseltakt der Framebuffer
static int test_fb() {
    int fail = 0;
    frame_t f;
    fill_fb();

    // Wechsel bei jedem Bild (fb_frames 0 wirkt wie 1): 0, 1, 2, 0
    static const int seq[4] = {0, 1, 2, 0};
    for (int i = 0; i < 4; i++) fail += expect_fb(render(PATTERN_FB, 0), "Reihenfolge", seq[i]);

    // Ein Testbild dazwischen laesst die Reihenfolge unberuehrt
    f = render(PATTERN_BARS, 0);
    fail += f.errors;
    if (which_fb(f) != -1) { std::printf("FEHLER: Testbild zeigt Framebuffer\n"); fail++; }

    // Jeder Puffer 2 Bilder lang: 1, 1, 2, 2, 0, 0
    static const int seq2[6] = {1, 1, 2, 2, 0, 0};
    for (int i = 0; i < 6; i++) fail += expect_fb(render(PATTERN_FB, 0, 0, 2), "Wechseltakt 2", seq2[i]);

    // Format-Bits wirken auch auf Framebuffer-Daten; naechster Puffer ist 1
    fail += expect_fb(render(PATTERN_FB, 0, FORMAT_SWAP_CBCR, 3), "FORMAT_SWAP_CBCR", 1, true, false);
    fail += expect_fb(render(PATTERN_FB, 0, FORMAT_SWAP_YC, 3),   "FORMAT_SWAP_YC",   1, false, true);
    fail += expect_fb(render(PATTERN_FB, 0, 0, 3),                "Wechseltakt 3",    1);
    fail += expect_fb(render(PATTERN_FB, 0, 0, 3),                "Wechseltakt 3",    2);
    return fail;
}

static int clamp8(double v) { return v < 0 ? 0 : v > 255 ? 255 : (int)std::lround(v); }

// YCbCr 4:2:2 (BT.709, begrenzt) -> RGB, so wie es der HDMI-Sender rechnen soll
static void to_rgb(const frame_t &f, int x, int y, int rgb[3]) {
    int i = y * FRAME_WIDTH + (x & ~1);
    double Y = (f.y[y * FRAME_WIDTH + x] - 16) * 255.0 / 219.0;
    double cb = (f.c[i] - 128) * 255.0 / 224.0, cr = (f.c[i + 1] - 128) * 255.0 / 224.0;
    rgb[0] = clamp8(Y + 1.5748 * cr);
    rgb[1] = clamp8(Y - 0.1873 * cb - 0.4681 * cr);
    rgb[2] = clamp8(Y + 1.8556 * cb);
}

static void write_ppm(const frame_t &f, const char *name) {
    FILE *fp = std::fopen(name, "wb");
    if (!fp) return;
    std::fprintf(fp, "P6\n%d %d\n255\n", FRAME_WIDTH, FRAME_HEIGHT);
    for (int y = 0; y < FRAME_HEIGHT; y++)
        for (int x = 0; x < FRAME_WIDTH; x++) {
            int rgb[3]; to_rgb(f, x, y, rgb);
            unsigned char b[3] = {(unsigned char)rgb[0], (unsigned char)rgb[1], (unsigned char)rgb[2]};
            std::fwrite(b, 1, 3, fp);
        }
    std::fclose(fp);
}

// Erwartete Farbe an einer Stelle pruefen (Toleranz wegen Rundung der Farbwandlung)
static int expect(const frame_t &f, const char *what, int x, int y, int r, int g, int b) {
    int rgb[3]; to_rgb(f, x, y, rgb);
    bool ok = std::abs(rgb[0] - r) <= 3 && std::abs(rgb[1] - g) <= 3 && std::abs(rgb[2] - b) <= 3;
    if (!ok) std::printf("FEHLER %s (%d,%d): %d,%d,%d statt %d,%d,%d\n", what, x, y, rgb[0], rgb[1], rgb[2], r, g, b);
    return ok ? 0 : 1;
}

// Modell: Pixel, die eine Tabelle aus LINE_COUNT Eintraegen gruen faerbt
static std::vector<unsigned char> model_lines(const uint64_t *words) {
    std::vector<unsigned char> want(FRAME_WIDTH * FRAME_HEIGHT, 0);
    for (int n = 0; n < LINE_COUNT; n++) {
        uint64_t w0 = words[2 * n], w1 = words[2 * n + 1];
        if (!((w0 >> 48) & LINE_VALID)) continue;
        // Abschnitt jeder Zeile direkt aus dem Eintrag: x + k * slope bis x + (k + 1) * slope
        long long x0 = (long long)(w0 & 0xFFFF) << LINE_FRAC;
        long long slope = (int32_t)(w1 & 0xFFFFFFFF), one = 1LL << LINE_FRAC;
        int y0 = (w0 >> 16) & 0xFFFF, y1 = (w0 >> 32) & 0xFFFF;
        bool flat = ((w0 >> 48) & LINE_FLAT) != 0;
        for (int k = 0; k <= y1 - y0; k++) {
            long long a = x0 + k * slope, b = a + slope;
            long long lo = std::min(a, b) / one, hi = (std::max(a, b) + one - 1) / one;   // [lo, hi)
            lo &= ~1LL; hi = (hi + 1) & ~1LL;                                             // Pixelpaare
            for (int dy = 0; dy <= (flat ? 1 : 0); dy++) {
                int y = y0 + k + dy;
                if (y >= FRAME_HEIGHT) continue;
                for (long long x = lo; x < hi && x < FRAME_WIDTH; x++) want[y * FRAME_WIDTH + x] = 1;
            }
        }
    }
    return want;
}

// Bild gegen Modell: gruen genau dort, wo das Modell es erwartet, sonst schwarz. Unabhaengig
// vom Tabellenformat: jedes gruene Pixel liegt nah an einer der Strecken (pts, n Stueck).
static int compare_lines(const frame_t &f, const std::vector<unsigned char> &want,
                         const double (*pts)[4], int n, double tol, const char *what) {
    int green = 0, wrong = 0, other = 0, far = 0;
    for (int y = 0; y < FRAME_HEIGHT; y++)
        for (int x = 0; x < FRAME_WIDTH; x++) {
            int i = y * FRAME_WIDTH + x;
            bool on = (f.y[i] == 173);
            if (!on && f.y[i] != 16) other++;
            if (on != (want[i] != 0)) wrong++;
            if (!on) continue;
            green++;
            double best = 1e9;
            for (int k = 0; k < n; k++) {
                const double *p = pts[k];
                double vx = p[2] - p[0], vy = p[3] - p[1], ux = x - p[0], uy = y - p[1];
                double l2 = vx * vx + vy * vy, t = l2 > 0 ? (ux * vx + uy * vy) / l2 : 0;
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                best = std::min(best, std::hypot(ux - t * vx, uy - t * vy));
            }
            if (best > tol) far++;
        }
    std::printf("%s: %d Pixel gruen, %d anders als das Modell, %d weiter als %.1f Pixel von einer Strecke, "
                "%d in anderer Farbe\n", what, green, wrong, far, tol, other);
    return (wrong || far || other) ? 1 : 0;
}

// Festes Linienbild: Tabelle gegen die Endpunkte, Bild gegen das Modell
static int test_lines(const frame_t &f) {
    int fail = 0;
    uint64_t words[2 * LINE_COUNT];
    double pts[LINE_COUNT][4];

    for (int n = 0; n < LINE_COUNT; n++) {
        const int *p = LINES_POINTS[n];
        uint64_t w[2];
        line_encode(p[0], p[1], p[2], p[3], w);
        if (w[0] != LINES_ROM[2 * n] || w[1] != LINES_ROM[2 * n + 1]) {
            std::printf("FEHLER Linie %d: Tabelle passt nicht zu den Endpunkten\n", n);
            fail++;
        }
        words[2 * n] = LINES_ROM[2 * n].to_uint64();
        words[2 * n + 1] = LINES_ROM[2 * n + 1].to_uint64();
        for (int k = 0; k < 4; k++) pts[n][k] = p[k];
    }
    fail += compare_lines(f, model_lines(words), pts, LINE_COUNT, 2.5, "Linien (fest)");

    // Jede Strecke ist durchgehend gezeichnet
    for (int n = 0; n < LINE_COUNT; n++) {
        const int *p = LINES_POINTS[n];
        int gaps = 0;
        for (int j = 0; j <= 2000; j++) {
            int x = (int)std::lround(p[0] + (p[2] - p[0]) * j / 2000.0);
            int y = (int)std::lround(p[1] + (p[3] - p[1]) * j / 2000.0);
            // der Core zeichnet Pixelpaare und rundet anders als hier: ein Pixel Abstand ist erlaubt
            bool on = false;
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -2; dx <= 2; dx++) {
                    int xx = x + dx, yy = y + dy;
                    if (xx >= 0 && xx < FRAME_WIDTH && yy >= 0 && yy < FRAME_HEIGHT && f.y[yy * FRAME_WIDTH + xx] == 173) on = true;
                }
            if (!on) gaps++;
        }
        if (gaps) { std::printf("FEHLER Linie %d: %d Luecken\n", n, gaps); fail++; }
    }

    // Endpunkte und Mitte jeder Linie muessen gruen sein, die Bildecken schwarz
    for (int n = 0; n < LINE_COUNT; n++) {
        const int *p = LINES_POINTS[n];
        fail += expect(f, "Linie Anfang", p[0], p[1], 0, 255, 0);
        fail += expect(f, "Linie Ende",   p[2], p[3], 0, 255, 0);
    }
    fail += expect(f, "Stern waagerecht", FRAME_WIDTH / 2 + 250, FRAME_HEIGHT / 2, 0, 255, 0);
    fail += expect(f, "Stern senkrecht",  FRAME_WIDTH / 2, FRAME_HEIGHT / 2 - 250, 0, 255, 0);
    fail += expect(f, "Stern diagonal",   FRAME_WIDTH / 2 + 180, FRAME_HEIGHT / 2 + 180, 0, 255, 0);
    fail += expect(f, "Ecke",             0, 0, 0, 0, 0);
    fail += expect(f, "Ecke",             FRAME_WIDTH - 1, FRAME_HEIGHT - 1, 0, 0, 0);
    fail += expect(f, "Sternmitte",       FRAME_WIDTH / 2, FRAME_HEIGHT / 2, 0, 0, 0);
    return fail;
}

// Puffer buf der Tabelle mit Strecken fuellen (beschnitten wie in der Anwendung)
static void fill_table(int buf, const double (*pts)[4], int n, uint64_t words[2 * LINE_COUNT]) {
    for (int k = 0; k < LINE_COUNT; k++) {
        uint64_t w[2] = {0, 0};
        if (k < n) line_clip(pts[k][0], pts[k][1], pts[k][2], pts[k][3], w);
        words[2 * k] = w[0]; words[2 * k + 1] = w[1];
        lines_mem[buf * LINE_BUF_WORDS + 2 * k]     = w[0];
        lines_mem[buf * LINE_BUF_WORDS + 2 * k + 1] = w[1];
    }
}

// Linien aus der Tabelle: beide Puffer, Umschalten zwischen den Bildern, Beschneiden
static int test_table(const frame_t &rom_frame) {
    int fail = 0;
    frame_t f;

    // Puffer 1 = festes Bild -> gleiches Bild wie aus dem ROM
    for (int j = 0; j < 2 * LINE_COUNT; j++) lines_mem[LINE_BUF_WORDS + j] = LINES_ROM[j];
    // Puffer 0: wenige Linien, teils ueber den Rand, eine ganz ausserhalb
    static const double few[5][4] = {
        {-300, 100, 500, 900},          // links abgeschnitten
        {1500, -50, 2100, 700},         // oben und rechts abgeschnitten
        {100, 1000, 1800, 1200},        // flach, unten abgeschnitten
        {-100, -100, -10, 500},         // ganz ausserhalb
        {960, 540, 960, 540},           // ein einzelner Punkt
    };
    uint64_t words[2 * LINE_COUNT];
    fill_table(0, few, 5, words);
    if (words[6] != 0 || words[7] != 0) { std::printf("FEHLER: Linie ausserhalb nicht leer\n"); fail++; }

    lines_ctrl = LINES_CTRL_TABLE | LINES_CTRL_BUF1;
    f = render(PATTERN_LINES, 0);
    fail += f.errors;
    if (last_cur != (unsigned)lines_ctrl) { std::printf("FEHLER: lines_cur %u statt %d\n", last_cur, lines_ctrl); fail++; }
    if (f.y != rom_frame.y || f.c != rom_frame.c) { std::printf("FEHLER: Puffer 1 mit festem Bild sieht anders aus\n"); fail++; }

    lines_ctrl = LINES_CTRL_TABLE;
    f = render(PATTERN_LINES, 0);
    fail += f.errors;
    if (last_cur != (unsigned)lines_ctrl) { std::printf("FEHLER: lines_cur %u statt %d\n", last_cur, lines_ctrl); fail++; }
    fail += compare_lines(f, model_lines(words), few, 5, 3.0, "Linien (Tabelle, Rand)");
    write_ppm(f, "pattern8_table.ppm");
    fail += expect(f, "Rand links",  0, 400, 0, 255, 0);               // (-300,100)-(500,900) trifft x=0 bei y=400
    fail += expect(f, "Rand unten",  772, FRAME_HEIGHT - 1, 0, 255, 0); // (100,1000)-(1800,1200) trifft y=1079 bei x=772
    fail += expect(f, "Punkt",       960, 540, 0, 255, 0);
    fail += expect(f, "leer",        1000, 300, 0, 0, 0);

    // Zufaellige Linien ueber den ganzen Bereich und darueber hinaus
    double rnd[LINE_COUNT][4];
    unsigned seed = 12345;
    for (int k = 0; k < LINE_COUNT; k++)
        for (int c = 0; c < 4; c++) {
            seed = seed * 1103515245u + 12345u;
            double v = (seed >> 8) % 10000 / 10000.0;
            rnd[k][c] = (c % 2 == 0) ? v * 2400 - 240 : v * 1400 - 160;
        }
    fill_table(1, rnd, LINE_COUNT, words);
    lines_ctrl = LINES_CTRL_TABLE | LINES_CTRL_BUF1;
    f = render(PATTERN_LINES, 0);
    fail += f.errors;
    fail += compare_lines(f, model_lines(words), rnd, LINE_COUNT, 3.0, "Linien (Tabelle, zufaellig)");
    write_ppm(f, "pattern8_random.ppm");

    // Zurueck zum festen Bild
    lines_ctrl = 0;
    f = render(PATTERN_LINES, 0);
    fail += f.errors;
    if (f.y != rom_frame.y) { std::printf("FEHLER: festes Bild nach der Tabelle anders\n"); fail++; }
    return fail;
}

int main() {
    int fail = 0;
    frame_t f;

#ifdef COSIM
    fail += test_fb();
#else

    // Das Register wird erst am Bildanfang uebernommen: Bild 0 startet mit dem Reset-Wert (schwarz)
    f = render(PATTERN_BLACK, 0);
    fail += f.errors;
    fail += expect(f, "schwarz", FRAME_WIDTH / 2, FRAME_HEIGHT / 2, 0, 0, 0);
    if (f.y[0] != 16 || f.c[0] != 128 || f.c[1] != 128) { std::printf("FEHLER: Schwarz ist nicht Y=16, C=128\n"); fail++; }
    write_ppm(f, "pattern0.ppm");

    f = render(PATTERN_BARS, 0);
    fail += f.errors;
    static const int bars[8][3] = {{255,255,255},{255,255,0},{0,255,255},{0,255,0},{255,0,255},{255,0,0},{0,0,255},{0,0,0}};
    const int BAR = FRAME_WIDTH / 8;
    for (int i = 0; i < 8; i++) {
        fail += expect(f, "Balken", i * BAR + BAR / 2, FRAME_HEIGHT / 2, bars[i][0], bars[i][1], bars[i][2]);
        fail += expect(f, "Balken links",  i * BAR + 2,       5, bars[i][0], bars[i][1], bars[i][2]);
        fail += expect(f, "Balken rechts", i * BAR + BAR - 2, 5, bars[i][0], bars[i][1], bars[i][2]);
    }
    if (f.y[BAR / 2] != 235) { std::printf("FEHLER: Weiss ist nicht Y=235 (%d)\n", f.y[BAR / 2]); fail++; }
    write_ppm(f, "pattern1.ppm");

    f = render(PATTERN_RAMP, 0);
    fail += f.errors;
    fail += expect(f, "Graukeil links", 0, 10, 0, 0, 0);
    fail += expect(f, "Graukeil Mitte", FRAME_WIDTH / 2, 10, 127, 127, 127);
    fail += expect(f, "Graukeil rechts", FRAME_WIDTH - 1, 10, 254, 254, 254);
    write_ppm(f, "pattern2.ppm");

    f = render(PATTERN_CHECKER, 0);
    fail += f.errors;
    fail += expect(f, "Schachbrett", 10, 10, 0, 0, 0);
    fail += expect(f, "Schachbrett", 74, 10, 255, 255, 255);
    fail += expect(f, "Schachbrett", 74, 74, 0, 0, 0);
    write_ppm(f, "pattern3.ppm");

    f = render(PATTERN_SOLID, 0xFF8000);
    fail += f.errors;
    fail += expect(f, "Vollflaeche orange", 5, 5, 255, 128, 0);
    fail += expect(f, "Vollflaeche orange", FRAME_WIDTH - 1, FRAME_HEIGHT - 1, 255, 128, 0);
    write_ppm(f, "pattern4.ppm");

    // Bewegtes Quadrat: muss sich von Bild zu Bild verschieben
    f = render(PATTERN_BOX, 0);
    fail += f.errors;
    write_ppm(f, "pattern5.ppm");
    frame_t g = render(PATTERN_BOX, 0);
    fail += g.errors;
    if (f.y == g.y) { std::printf("FEHLER: Quadrat bewegt sich nicht\n"); fail++; }
    fail += expect(g, "Hintergrund", FRAME_WIDTH - 10, FRAME_HEIGHT - 10, 0, 0, 96);

    // Farbverlauf: weich (keine Spruenge zwischen Nachbarpixeln) und von Bild zu Bild veraendert
    f = render(PATTERN_GRADIENT, 0);
    fail += f.errors;
    write_ppm(f, "pattern6.ppm");
    int max_step = 0;
    for (int y = 0; y < FRAME_HEIGHT; y++)
        for (int x = 1; x < FRAME_WIDTH; x++) {
            int d = std::abs(f.y[y * FRAME_WIDTH + x] - f.y[y * FRAME_WIDTH + x - 1]);
            if (d > max_step) max_step = d;
        }
    for (int y = 1; y < FRAME_HEIGHT; y++)
        for (int x = 0; x < FRAME_WIDTH; x++) {
            int d = std::abs(f.y[y * FRAME_WIDTH + x] - f.y[(y - 1) * FRAME_WIDTH + x]);
            if (d > max_step) max_step = d;
        }
    if (max_step > 2) { std::printf("FEHLER: Farbverlauf hat Spruenge (max. Y-Schritt %d)\n", max_step); fail++; }
    int ymin = 255, ymax = 0;
    for (size_t i = 0; i < f.y.size(); i++) { if (f.y[i] < ymin) ymin = f.y[i]; if (f.y[i] > ymax) ymax = f.y[i]; }
    if (ymax - ymin < 100) { std::printf("FEHLER: Farbverlauf nutzt kaum Wertebereich (%d..%d)\n", ymin, ymax); fail++; }
    for (int i = 0; i < 20; i++) g = render(PATTERN_GRADIENT, 0);
    fail += g.errors;
    if (f.y == g.y) { std::printf("FEHLER: Farbverlauf aendert sich nicht\n"); fail++; }
    write_ppm(g, "pattern6_later.ppm");

    // Linien: zwei Bilder nacheinander muessen gleich sein (die Tabelle wird je Bild neu geladen)
    f = render(PATTERN_LINES, 0);
    fail += f.errors;
    fail += test_lines(f);
    write_ppm(f, "pattern8.ppm");
    g = render(PATTERN_LINES, 0);
    fail += g.errors;
    if (f.y != g.y || f.c != g.c) { std::printf("FEHLER: Linienbild aendert sich\n"); fail++; }
    fail += test_table(f);

    // Format-Bits: Y/C getauscht -> Y steht im oberen Byte
    f = render(PATTERN_BARS, 0, FORMAT_SWAP_YC);
    fail += f.errors;
    if (f.c[BAR / 2] != 235 || f.y[BAR / 2] != 128) { std::printf("FEHLER: FORMAT_SWAP_YC\n"); fail++; }
    // Cb/Cr getauscht: im roten Balken (Cb < 128 < Cr) steht dann Cr auf dem geraden Pixel
    f = render(PATTERN_BARS, 0, FORMAT_SWAP_CBCR);
    fail += f.errors;
    if (!(f.c[5 * BAR + 80] > 128 && f.c[5 * BAR + 81] < 128)) { std::printf("FEHLER: FORMAT_SWAP_CBCR\n"); fail++; }

    fail += test_fb();
#endif

    std::printf(fail ? "TEST FEHLGESCHLAGEN (%d Fehler)\n" : "TEST OK\n", fail);
    return fail ? 1 : 0;
}
