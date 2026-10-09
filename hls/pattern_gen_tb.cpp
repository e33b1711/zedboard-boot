// C-Testbench: rendert jedes Testbild, prueft Bildformat, Rahmenmarken und Stichproben
// und schreibt die Bilder als PPM (zurueckgewandelt nach RGB) ins Arbeitsverzeichnis.
#include "pattern_gen.h"
#include <cmath>
#include <cstdio>
#include <vector>

struct frame_t { std::vector<unsigned char> y, c; int errors; };

static frame_t render(int pattern, unsigned color, int format = 0) {
    frame_t f; f.errors = 0;
    hls::stream<video_t> s;
    for (int yy = 0; yy < FRAME_HEIGHT; yy++) {
        for (int xx = 0; xx < FRAME_WIDTH; xx++) {
            pattern_gen(pattern, color, format, s);
            video_t v = s.read();
            if ((v.user == 1) != (xx == 0 && yy == 0))     f.errors++;   // Start of Frame
            if ((v.last == 1) != (xx == FRAME_WIDTH - 1))  f.errors++;   // End of Line
            f.y.push_back(v.data.to_uint() & 0xFF);
            f.c.push_back(v.data.to_uint() >> 8);
        }
    }
    return f;
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

int main() {
    int fail = 0;
    char name[64];
    frame_t f;

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

    // Format-Bits: Y/C getauscht -> Y steht im oberen Byte
    f = render(PATTERN_BARS, 0, FORMAT_SWAP_YC);
    fail += f.errors;
    if (f.c[BAR / 2] != 235 || f.y[BAR / 2] != 128) { std::printf("FEHLER: FORMAT_SWAP_YC\n"); fail++; }
    // Cb/Cr getauscht: im roten Balken (Cb < 128 < Cr) steht dann Cr auf dem geraden Pixel
    f = render(PATTERN_BARS, 0, FORMAT_SWAP_CBCR);
    fail += f.errors;
    if (!(f.c[5 * BAR + 80] > 128 && f.c[5 * BAR + 81] < 128)) { std::printf("FEHLER: FORMAT_SWAP_CBCR\n"); fail++; }

    std::printf(fail ? "TEST FEHLGESCHLAGEN (%d Fehler)\n" : "TEST OK\n", fail);
    return fail ? 1 : 0;
}
