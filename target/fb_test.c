/* =============================================================================
 *  Framebuffer des HDMI-Testbild-Generators (HLS-Core pattern_gen) beschreiben
 *
 *  Aufruf: fb_test                    alle drei Puffer fuellen (stern, barcode, schrift),
 *                                     Adressen eintragen und Framebuffer-Ausgabe einschalten
 *          fb_test 1 balken           Puffer 1 (0..2) mit einem Bild fuellen
 *          fb_test 0 farbe ff8000     Vollflaeche in der Farbe RRGGBB
 *          fb_test 2 foto.ppm         PPM-Datei (binaer, "P6") laden, wird eingepasst
 *          fb_test -o DIR [...]       Test auf dem PC: statt in den Speicher nach
 *                                     DIR/fbN.yuv und DIR/fbN.ppm schreiben
 *
 *  Bilder: stern    neunzackiger Stern, rot auf schwarz
 *          barcode  Strichcode 742617000027 (UPC-A), gut zum Pruefen der Schaerfe
 *          schrift  Schriftzug mit Bildstoerungen
 *          balken   8 Farbbalken
 *          verlauf  Farbverlauf
 *          ziffer   Puffernummer bildschirmfuellend
 *          farbe    Vollflaeche
 *
 *  Speicher: drei Bereiche aus dem Device Tree (reserved-memory/framebuffer@...),
 *            1920x1080, 2 Byte pro Pixel: Y, Cb, Y, Cr ("YUYV", BT.709, begrenzter Bereich)
 *  Register: fb_addr0..2 0x28/0x30/0x38, fb_frames 0x40, pattern 0x10
 * ============================================================================= */
#include <fcntl.h>
#include <glob.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define W 1920
#define H 1080
#define FB_COUNT 3
#define FRAME_BYTES (W * H * 2)

#define REG_PATTERN   0x10
#define REG_FB_ADDR0  0x28          /* fb_addr1 = +8, fb_addr2 = +16 */
#define REG_FB_FRAMES 0x40
#define PATTERN_FB    7
#define DEFAULT_FRAMES 120          /* 2 Sekunden pro Puffer */

static uint8_t rgb[H][W][3];        /* Bild in RGB, wird am Ende nach YUYV gewandelt */

static void die(const char *msg) { perror(msg); exit(1); }

static void put(int x, int y, int r, int g, int b) {
    if (x < 0 || x >= W || y < 0 || y >= H) return;
    rgb[y][x][0] = r < 0 ? 0 : r > 255 ? 255 : r;
    rgb[y][x][1] = g < 0 ? 0 : g > 255 ? 255 : g;
    rgb[y][x][2] = b < 0 ? 0 : b > 255 ? 255 : b;
}

static void fill(int r, int g, int b) {
    for (int y = 0; y < H; y++) for (int x = 0; x < W; x++) put(x, y, r, g, b);
}

static void rect(int x0, int y0, int w, int h, int r, int g, int b) {
    for (int y = y0; y < y0 + h; y++) for (int x = x0; x < x0 + w; x++) put(x, y, r, g, b);
}

/* Zufallszahl 0..255 aus einer Position, bei jedem Lauf gleich */
static unsigned noise(unsigned x, unsigned y) {
    unsigned h = x * 374761393u + y * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return (h ^ (h >> 16)) & 0xFF;
}

static double smooth(double e0, double e1, double v) {
    double t = (v - e0) / (e1 - e0);
    t = t < 0 ? 0 : t > 1 ? 1 : t;
    return t * t * (3 - 2 * t);
}

/* --- Schrift: 5x7 Punkte, Zeilen von oben, Bit 4 = links ---------------------- */
static const char FONT_CHARS[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ()-. ";
static const uint8_t FONT[][7] = {
    {0x0E,0x11,0x13,0x15,0x19,0x11,0x0E}, {0x04,0x0C,0x04,0x04,0x04,0x04,0x0E},
    {0x0E,0x11,0x01,0x02,0x04,0x08,0x1F}, {0x1F,0x02,0x04,0x02,0x01,0x11,0x0E},
    {0x02,0x06,0x0A,0x12,0x1F,0x02,0x02}, {0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E},
    {0x06,0x08,0x10,0x1E,0x11,0x11,0x0E}, {0x1F,0x01,0x02,0x04,0x08,0x08,0x08},
    {0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E}, {0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C},
    {0x0E,0x11,0x11,0x1F,0x11,0x11,0x11}, {0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E},
    {0x0E,0x11,0x10,0x10,0x10,0x11,0x0E}, {0x1C,0x12,0x11,0x11,0x11,0x12,0x1C},
    {0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F}, {0x1F,0x10,0x10,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x10,0x17,0x11,0x11,0x0F}, {0x11,0x11,0x11,0x1F,0x11,0x11,0x11},
    {0x0E,0x04,0x04,0x04,0x04,0x04,0x0E}, {0x07,0x02,0x02,0x02,0x02,0x12,0x0C},
    {0x11,0x12,0x14,0x18,0x14,0x12,0x11}, {0x10,0x10,0x10,0x10,0x10,0x10,0x1F},
    {0x11,0x1B,0x15,0x15,0x11,0x11,0x11}, {0x11,0x11,0x19,0x15,0x13,0x11,0x11},
    {0x0E,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x1E,0x11,0x11,0x1E,0x10,0x10,0x10},
    {0x0E,0x11,0x11,0x11,0x15,0x12,0x0D}, {0x1E,0x11,0x11,0x1E,0x14,0x12,0x11},
    {0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E}, {0x1F,0x04,0x04,0x04,0x04,0x04,0x04},
    {0x11,0x11,0x11,0x11,0x11,0x11,0x0E}, {0x11,0x11,0x11,0x11,0x11,0x0A,0x04},
    {0x11,0x11,0x11,0x15,0x15,0x15,0x0A}, {0x11,0x11,0x0A,0x04,0x0A,0x11,0x11},
    {0x11,0x11,0x11,0x0A,0x04,0x04,0x04}, {0x1F,0x01,0x02,0x04,0x08,0x10,0x1F},
    {0x02,0x04,0x08,0x08,0x08,0x04,0x02}, {0x08,0x04,0x02,0x02,0x02,0x04,0x08},
    {0x00,0x00,0x00,0x1F,0x00,0x00,0x00}, {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00},
};

/* Ist der Punkt (px, py) eines Textes gesetzt? Zeichenzelle 6x7 Punkte */
static int text_dot(const char *s, int px, int py) {
    if (px < 0 || py < 0 || py >= 7 || px % 6 == 5) return 0;
    int i = px / 6;
    if (i >= (int)strlen(s)) return 0;
    const char *p = strchr(FONT_CHARS, s[i]);
    if (!p) return 0;
    return (FONT[p - FONT_CHARS][py] >> (4 - px % 6)) & 1;
}

static int text_width(const char *s, int scale) { return ((int)strlen(s) * 6 - 1) * scale; }

static void text(const char *s, int x0, int y0, int scale, int r, int g, int b) {
    for (int y = 0; y < 7 * scale; y++)
        for (int x = 0; x < text_width(s, scale); x++)
            if (text_dot(s, x / scale, y / scale)) put(x0 + x, y0 + y, r, g, b);
}

/* Puffernummer unten rechts, damit man den Wechsel auch bei eigenen Bildern erkennt */
static void tag(int n) {
    char s[8];
    snprintf(s, sizeof s, "FB%d", n);
    rect(W - 150, H - 70, 150, 70, 0, 0, 0);
    text(s, W - 132, H - 56, 6, 255, 255, 255);
}

/* --- Bilder ------------------------------------------------------------------- */

/* Neunzackiger Stern: 9 Punkte auf einem Kreis, jeder mit dem viertnaechsten verbunden */
static void img_stern(void) {
    const double cx = W / 2.0, cy = H / 2.0, R = 440;
    double vx[9], vy[9];
    for (int i = 0; i < 9; i++) {
        double a = (-90 + 40 * i) * M_PI / 180;
        vx[i] = cx + R * cos(a);
        vy[i] = cy + R * sin(a);
    }
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            double d = 1e9;                     /* Abstand zur naechsten Linie */
            for (int i = 0; i < 9; i++) {
                int j = (i + 4) % 9;
                double ax = vx[i], ay = vy[i], bx = vx[j] - ax, by = vy[j] - ay;
                double t = ((x - ax) * bx + (y - ay) * by) / (bx * bx + by * by);
                t = t < 0 ? 0 : t > 1 ? 1 : t;
                double e = hypot(x - ax - t * bx, y - ay - t * by);
                if (e < d) d = e;
            }
            double rr = hypot(x - cx, y - cy);
            double ring = fabs(rr - (R + 40));
            if (ring < d) d = ring;

            double line = 1 - smooth(5, 8, d);              /* Linie, 2 x 6,5 Pixel breit */
            double glow = exp(-d / 45) * 0.55;              /* roter Schein um die Linien */
            double vign = 1 - smooth(300, 1100, rr);        /* zum Rand hin dunkler */
            int grain = (int)noise(x, y) - 128;
            double r = 14 * vign + 215 * glow + 255 * line + grain * 0.06;
            double g = 4 * vign + 20 * glow * glow + 30 * line + grain * 0.03;
            double b = 6 * vign + 25 * glow * glow + 30 * line + grain * 0.03;
            put(x, y, (int)r, (int)g, (int)b);
        }
    text("9", 60, 50, 10, 150, 0, 0);
}

/* Strichcode UPC-A: 95 Module, je 16 Pixel breit */
static void img_barcode(void) {
    static const char *DIGITS = "742617000027";
    static const uint8_t LEFT[10] = {0x0D,0x19,0x13,0x3D,0x23,0x31,0x2F,0x3B,0x37,0x0B};
    const int M = 16, x0 = (W - 95 * M) / 2, y0 = 120, bar_h = 660, guard_h = 730;
    uint8_t mod[95];
    int n = 0;

    mod[n++] = 1; mod[n++] = 0; mod[n++] = 1;
    for (int i = 0; i < 6; i++)
        for (int b = 6; b >= 0; b--) mod[n++] = (LEFT[DIGITS[i] - '0'] >> b) & 1;
    mod[n++] = 0; mod[n++] = 1; mod[n++] = 0; mod[n++] = 1; mod[n++] = 0;
    for (int i = 6; i < 12; i++)
        for (int b = 6; b >= 0; b--) mod[n++] = !((LEFT[DIGITS[i] - '0'] >> b) & 1);
    mod[n++] = 1; mod[n++] = 0; mod[n++] = 1;

    for (int y = 0; y < H; y++)                    /* Papier mit leichter Koernung */
        for (int x = 0; x < W; x++) {
            int v = 226 + (int)noise(x, y) / 16;
            put(x, y, v, v, v - 10);
        }
    for (int i = 0; i < 95; i++) {
        int guard = i < 3 || i >= 92 || (i >= 45 && i < 50) || i < 10 || i >= 85;
        if (mod[i]) rect(x0 + i * M, y0, M, guard ? guard_h : bar_h, 10, 10, 10);
    }
    /* Ziffern: erste und letzte ausserhalb, je fuenf unter den beiden Haelften */
    char s[6] = "";
    const int sc = 9, ty = y0 + bar_h + 12;
    s[0] = DIGITS[0]; s[1] = 0;
    text(s, x0 - 80, ty, sc, 10, 10, 10);
    memcpy(s, DIGITS + 1, 5); s[5] = 0;
    text(s, x0 + 10 * M + (35 * M - text_width(s, sc)) / 2, ty, sc, 10, 10, 10);
    memcpy(s, DIGITS + 6, 5); s[5] = 0;
    text(s, x0 + 50 * M + (35 * M - text_width(s, sc)) / 2, ty, sc, 10, 10, 10);
    s[0] = DIGITS[11]; s[1] = 0;
    text(s, x0 + 95 * M + 35, ty, sc, 10, 10, 10);
    text("(SIC)", (W - text_width("(SIC)", 8)) / 2, H - 100, 8, 170, 0, 0);
}

/* Schriftzug mit zeilenweisem Versatz und auseinanderlaufenden Farbkanaelen */
static void img_schrift(void) {
    static const char *NAME = "SLIPKNOT", *SUB = "IOWA - 515";
    const int sc = 36, tw = text_width(NAME, sc), tx = (W - tw) / 2, ty = 330;
    const int sc2 = 10, tx2 = (W - text_width(SUB, sc2)) / 2, ty2 = 720;

    for (int y = 0; y < H; y++) {
        int band = y / 12;                              /* Stoerung in Streifen */
        int shift = 0, split = 4;
        if (noise(band, 7) > 224) { shift = ((int)noise(band, 9) - 128) / 3; split = 12; }
        for (int x = 0; x < W; x++) {
            int v[3];
            for (int ch = 0; ch < 3; ch++) {
                int xs = x - shift + (ch - 1) * split;  /* Rot nach links, Blau nach rechts */
                v[ch] = text_dot(NAME, (xs - tx) / sc - (xs < tx), (y - ty) / sc - (y < ty)) ||
                        text_dot(SUB, (xs - tx2) / sc2 - (xs < tx2), (y - ty2) / sc2 - (y < ty2));
            }
            int grain = noise(x, y);
            int scan = (y & 3) == 3 ? 60 : 100;         /* Zeilenstruktur wie auf einer Roehre */
            int back = 30 * y / H + (grain > 250 ? 60 : grain / 24);
            int r = (v[0] ? 235 : back + 10) * scan / 100;
            int g = (v[1] ? 215 : back / 4) * scan / 100;
            int b = (v[2] ? 215 : back / 4) * scan / 100;
            if (v[0] && !v[1]) r = 255 * scan / 100;
            put(x, y, r, g, b);
        }
    }
}

static void img_balken(void) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++) {
            int bar = 7 - x / (W / 8);                  /* weiss, gelb, cyan, gruen, ... */
            put(x, y, bar & 2 ? 255 : 0, bar & 4 ? 255 : 0, bar & 1 ? 255 : 0);
        }
}

static void img_verlauf(void) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x++)
            put(x, y, 255 * x / (W - 1), 255 * y / (H - 1), 255 - 255 * (x + y) / (W + H - 2));
}

static void img_ziffer(int n) {
    static const int COL[3][3] = {{150, 0, 0}, {0, 110, 0}, {0, 0, 160}};
    char s[2] = {(char)('0' + n), 0};
    fill(COL[n % 3][0], COL[n % 3][1], COL[n % 3][2]);
    text(s, (W - 5 * 130) / 2, (H - 7 * 130) / 2, 130, 255, 255, 255);
}

/* PPM (P6, 8 Bit) laden und unter Erhalt des Seitenverhaeltnisses einpassen */
static void img_ppm(const char *name) {
    FILE *f = fopen(name, "rb");
    if (!f) die(name);
    int w = 0, h = 0, max = 0, field = 0, c;
    char magic[3] = "";
    if (fread(magic, 1, 2, f) != 2 || strcmp(magic, "P6")) { fprintf(stderr, "%s: kein binaeres PPM (P6)\n", name); exit(1); }
    while (field < 3) {                                  /* Kopf: Breite, Hoehe, Maximalwert */
        c = fgetc(f);
        if (c == '#') { while (c != '\n' && c != EOF) c = fgetc(f); continue; }
        if (c == EOF) break;
        if (c >= '0' && c <= '9') {
            int v = 0;
            while (c >= '0' && c <= '9') { v = v * 10 + c - '0'; c = fgetc(f); }
            if (field == 0) w = v; else if (field == 1) h = v; else max = v;
            field++;
        }
    }
    if (field != 3 || w < 1 || h < 1 || w > 16384 || h > 16384 || max != 255) {
        fprintf(stderr, "%s: PPM-Kopf nicht lesbar oder nicht 8 Bit\n", name); exit(1);
    }
    uint8_t *src = malloc((size_t)w * h * 3);
    if (!src || fread(src, 3, (size_t)w * h, f) != (size_t)w * h) { fprintf(stderr, "%s: Bilddaten unvollstaendig\n", name); exit(1); }
    fclose(f);

    int dw = W, dh = (int)((long long)h * W / w);        /* erst auf volle Breite ... */
    if (dh > H) { dh = H; dw = (int)((long long)w * H / h); }   /* ... sonst auf volle Hoehe */
    fill(0, 0, 0);
    for (int y = 0; y < dh; y++)
        for (int x = 0; x < dw; x++) {
            const uint8_t *p = src + ((size_t)(y * h / dh) * w + x * w / dw) * 3;
            put((W - dw) / 2 + x, (H - dh) / 2 + y, p[0], p[1], p[2]);
        }
    free(src);
}

/* --- RGB -> YCbCr 4:2:2 (BT.709, begrenzter Bereich), wie im HLS-Core ---------- */
static void to_yuyv(uint8_t *dst) {
    for (int y = 0; y < H; y++)
        for (int x = 0; x < W; x += 2) {
            int cb = 0, cr = 0;
            for (int k = 0; k < 2; k++) {
                int r = rgb[y][x + k][0], g = rgb[y][x + k][1], b = rgb[y][x + k][2];
                dst[0 + 2 * k] = 16 + ((748 * r + 2516 * g + 254 * b + 2048) >> 12);
                cb += -412 * r - 1387 * g + 1799 * b;
                cr += 1799 * r - 1634 * g - 165 * b;
            }
            dst[1] = 128 + ((cb + 4096) >> 13);          /* Farbe: Mittel beider Pixel */
            dst[3] = 128 + ((cr + 4096) >> 13);
            dst += 4;
        }
}

/* Rueckwandlung fuer die Kontrolle auf dem PC, so wie der HDMI-Sender rechnet */
static void write_ppm(const uint8_t *yuyv, const char *name) {
    FILE *f = fopen(name, "wb");
    if (!f) die(name);
    fprintf(f, "P6\n%d %d\n255\n", W, H);
    for (int i = 0; i < W * H; i++) {
        const uint8_t *p = yuyv + (i & ~1) * 2;
        double Y = (yuyv[2 * i] - 16) * 255.0 / 219, cb = (p[1] - 128) * 255.0 / 224, cr = (p[3] - 128) * 255.0 / 224;
        double v[3] = {Y + 1.5748 * cr, Y - 0.1873 * cb - 0.4681 * cr, Y + 1.8556 * cb};
        for (int k = 0; k < 3; k++) fputc(v[k] < 0 ? 0 : v[k] > 255 ? 255 : (int)(v[k] + 0.5), f);
    }
    fclose(f);
}

/* --- Zugriff auf Speicher und Register ---------------------------------------- */
static int mem_fd = -1;

static void *map(uint32_t addr, size_t len) {
    if (mem_fd < 0 && (mem_fd = open("/dev/mem", O_RDWR | O_SYNC)) < 0) die("/dev/mem");
    void *p = mmap(NULL, len, PROT_READ | PROT_WRITE, MAP_SHARED, mem_fd, addr);
    if (p == MAP_FAILED) die("mmap");
    return p;
}

/* Erste Adresse und Groesse aus einer "reg"-Datei des Device Tree (je 32 Bit, Big Endian) */
static int read_reg(const char *node, uint32_t *addr, uint32_t *size) {
    char path[512];
    uint8_t b[8];
    snprintf(path, sizeof path, "%s/reg", node);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(b, 1, 8, f);
    fclose(f);
    if (n != 8) return -1;
    *addr = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    *size = (uint32_t)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
    return 0;
}

static void usage(void) {
    fprintf(stderr,
        "Aufruf: fb_test                   alle drei Puffer fuellen und anzeigen\n"
        "        fb_test N BILD [RRGGBB]   Puffer N (0..2) fuellen\n"
        "        fb_test N DATEI.ppm       PPM-Datei in Puffer N laden\n"
        "        fb_test -o DIR [...]      auf dem PC: nach DIR/fbN.yuv und .ppm schreiben\n"
        "Bilder: stern barcode schrift balken verlauf ziffer farbe\n");
    exit(1);
}

static void draw(int n, const char *what, const char *arg) {
    if      (!strcmp(what, "stern"))   img_stern();
    else if (!strcmp(what, "barcode")) img_barcode();
    else if (!strcmp(what, "schrift")) img_schrift();
    else if (!strcmp(what, "balken"))  img_balken();
    else if (!strcmp(what, "verlauf")) img_verlauf();
    else if (!strcmp(what, "ziffer"))  { img_ziffer(n); return; }
    else if (!strcmp(what, "farbe")) {
        char *end;
        if (!arg || strlen(arg) != 6) usage();
        unsigned long c = strtoul(arg, &end, 16);
        if (*end) usage();
        fill(c >> 16 & 0xFF, c >> 8 & 0xFF, c & 0xFF);
        return;                                          /* Vollflaeche ohne Nummer */
    }
    else if (strlen(what) > 4 && !strcmp(what + strlen(what) - 4, ".ppm")) img_ppm(what);
    else usage();
    tag(n);
}

int main(int argc, char **argv) {
    static const char *DEFAULTS[FB_COUNT] = {"stern", "barcode", "schrift"};
    const char *out_dir = NULL;
    uint32_t fb_addr[FB_COUNT];
    volatile uint32_t *regs = NULL;

    if (argc >= 3 && !strcmp(argv[1], "-o")) { out_dir = argv[2]; argv += 2; argc -= 2; }
    if (argc != 1 && argc != 3 && argc != 4) usage();
    int single = -1;
    if (argc > 1) {
        if (strlen(argv[1]) != 1 || argv[1][0] < '0' || argv[1][0] >= '0' + FB_COUNT) usage();
        single = argv[1][0] - '0';
    }

    if (!out_dir) {
        /* Framebuffer und Core im Device Tree suchen */
        glob_t g;
        uint32_t size;
        if (glob("/proc/device-tree/reserved-memory/framebuffer@*", 0, NULL, &g) || g.gl_pathc != FB_COUNT) {
            fprintf(stderr, "Im Device Tree stehen nicht genau %d Knoten reserved-memory/framebuffer@...\n", FB_COUNT);
            return 1;
        }
        for (int n = 0; n < FB_COUNT; n++)               /* glob sortiert nach Adresse */
            if (read_reg(g.gl_pathv[n], &fb_addr[n], &size) || size < FRAME_BYTES || fb_addr[n] % 8) {
                fprintf(stderr, "%s: reg nicht lesbar, zu klein oder nicht durch 8 teilbar\n", g.gl_pathv[n]);
                return 1;
            }
        globfree(&g);

        uint32_t base;
        if (glob("/proc/device-tree/*/pattern_gen@*", 0, NULL, &g) || read_reg(g.gl_pathv[0], &base, &size)) {
            fprintf(stderr, "Knoten pattern_gen@... nicht im Device Tree gefunden\n");
            return 1;
        }
        globfree(&g);
        regs = map(base, 0x1000);
        for (int n = 0; n < FB_COUNT; n++) regs[(REG_FB_ADDR0 + 8 * n) / 4] = fb_addr[n];
    }

    uint8_t *yuyv = malloc(FRAME_BYTES);
    if (!yuyv) die("malloc");
    for (int n = 0; n < FB_COUNT; n++) {
        if (single >= 0 && n != single) continue;
        const char *what = single >= 0 ? argv[2] : DEFAULTS[n];
        draw(n, what, argc == 4 ? argv[3] : NULL);
        to_yuyv(yuyv);
        if (out_dir) {
            char name[512];
            snprintf(name, sizeof name, "%s/fb%d.yuv", out_dir, n);
            FILE *f = fopen(name, "wb");
            if (!f || fwrite(yuyv, 1, FRAME_BYTES, f) != FRAME_BYTES) die(name);
            fclose(f);
            snprintf(name, sizeof name, "%s/fb%d.ppm", out_dir, n);
            write_ppm(yuyv, name);
            printf("Puffer %d: %s -> %s\n", n, what, name);
        } else {
            uint8_t *fb = map(fb_addr[n], FRAME_BYTES);
            memcpy(fb, yuyv, FRAME_BYTES);
            int bad = memcmp(fb, yuyv, FRAME_BYTES) != 0;
            munmap(fb, FRAME_BYTES);
            printf("Puffer %d auf 0x%08X: %s%s\n", n, fb_addr[n], what, bad ? "  FEHLER: Ruecklesen stimmt nicht" : "");
            if (bad) return 1;
        }
    }

    if (regs && single < 0) {
        if ((regs[REG_FB_FRAMES / 4] & 0xFFFF) == 0) regs[REG_FB_FRAMES / 4] = DEFAULT_FRAMES;
        regs[REG_PATTERN / 4] = PATTERN_FB;
        printf("Framebuffer-Ausgabe an, Wechsel alle %u Bilder (aendern: set_pattern.sh 7 <Bilder>)\n",
               (unsigned)(regs[REG_FB_FRAMES / 4] & 0xFFFF));
    }
    return 0;
}
