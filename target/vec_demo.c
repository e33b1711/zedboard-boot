/* =============================================================================
 *  Bewegte Linien auf HDMI: liefert dem Testbild-Generator (pattern_gen, Testbild 8)
 *  fuer jedes Bild 32 neue Linien
 *
 *  Aufruf: vec_demo                 Wuerfel, Welle und Quadrat bewegen sich, bis Strg-C
 *          vec_demo -n 600          nach 600 Bildern aufhoeren
 *          vec_demo -r 80           mit Echtzeit-Prioritaet (SCHED_FIFO, 1..99)
 *          vec_demo -c 1            an einen Prozessorkern binden
 *          vec_demo -w 20           Test: jedes Bild 20 ms zusaetzlich rechnen (zu spaet)
 *          vec_demo -q              keine Ausgabe pro Sekunde
 *
 *  Ablauf pro Bild: am Anfang jedes Bildes uebernimmt der Core das Register lines_ctrl
 *          und gibt einen Interrupt (UIO, read auf /dev/uioN). Das Programm liest den
 *          Bildzaehler F, rechnet die Linien fuer Bild F + 1, schreibt sie in den Puffer,
 *          der gerade nicht gewaehlt ist, und waehlt ihn mit einem Schreibzugriff auf
 *          lines_ctrl. Der Core liest einen Puffer nur in den ersten Takten eines Bildes,
 *          und nur den gewaehlten; der Puffer, in den geschrieben wird, ist nie gewaehlt.
 *          Kommt die Wahl zu spaet, zeigt der Core das vorige Bild noch einmal, ganz.
 *  Zeit:   die Bewegung haengt am Bildzaehler des Cores, nicht an der Uhr: jedes Bild
 *          zeigt genau den Stand seiner Nummer, auch nach einem ausgelassenen Bild.
 *  Zaehlt: zu spaet (Wahl nicht vor dem naechsten Bildanfang angekommen), ausgelassene
 *          Interrupts (Bildzaehler springt um mehr als 1) und die Zeit vom Aufwachen bis
 *          zur Wahl.
 *  Nach dem Ende bleibt das letzte Bild stehen; set_pattern.sh 8 zeigt wieder das feste.
 *  Linien: target/lines.h (Steigung rechnen, am Bildrand beschneiden).
 *  Treiber: uio_pdrv_genirq mit of_id=generic-uio (/etc/modprobe.d/uio.conf).
 *  Rueckgabe 0, wenn kein Bild zu spaet kam.
 * ============================================================================= */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <math.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "lines.h"

/* Register von pattern_gen (Byte-Offsets aus xpattern_gen_hw.h) */
#define REG_PATTERN     0x10
#define REG_LINES_CTRL  0x48
#define REG_LINES_CUR   0x50
#define REG_FRAMES      0x60
#define REG_LINES_BASE  0x400            /* Speicher "lines": 2 Puffer zu 64 Worten zu 64 Bit */
#define MAP_BYTES       0x1000
#define BUF_WORDS       (2 * LINES_N)

#define CTRL_TABLE 1
#define CTRL_BUF1  2

static volatile uint32_t *regs;
static volatile sig_atomic_t stop;

static void on_signal(int sig) { (void)sig; stop = 1; }

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint32_t rd(unsigned off) { return regs[off / 4]; }
static void wr(unsigned off, uint32_t v) { regs[off / 4] = v; }

/* Basisadresse des Cores aus dem Device Tree */
static int core_addr(uint32_t *addr) {
    glob_t g;
    if (glob("/proc/device-tree/*/pattern_gen@*/reg", 0, NULL, &g) != 0) return -1;
    FILE *f = fopen(g.gl_pathv[0], "rb");
    globfree(&g);
    if (!f) return -1;
    uint8_t b[4];
    size_t n = fread(b, 1, 4, f);
    fclose(f);
    if (n != 4) return -1;
    *addr = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    return 0;
}

/* Nummer des UIO-Geraets, dessen erster Bereich bei addr liegt */
static int uio_number(uint32_t addr) {
    glob_t g;
    int found = -1;
    if (glob("/sys/class/uio/uio*/maps/map0/addr", 0, NULL, &g) != 0) return -1;
    for (size_t i = 0; i < g.gl_pathc && found < 0; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        unsigned long a;
        if (f && fscanf(f, "%lx", &a) == 1 && a == addr)
            sscanf(g.gl_pathv[i], "/sys/class/uio/uio%d", &found);
        if (f) fclose(f);
    }
    globfree(&g);
    return found;
}

static int open_core(void) {
    uint32_t addr;
    if (core_addr(&addr)) {
        fprintf(stderr, "FEHLER: pattern_gen nicht im Device Tree\n");
        exit(2);
    }
    int n = uio_number(addr);
    if (n < 0) {
        if (system("rmmod uio_pdrv_genirq 2>/dev/null; modprobe uio_pdrv_genirq of_id=generic-uio") != 0)
            fprintf(stderr, "WARN: modprobe uio_pdrv_genirq fehlgeschlagen\n");
        for (int i = 0; i < 20 && n < 0; i++) {     /* udev legt /dev/uioN erst an */
            usleep(50000);
            n = uio_number(addr);
        }
    }
    if (n < 0) {
        fprintf(stderr, "FEHLER: kein UIO-Geraet fuer pattern_gen@%08x "
                "(Knoten ohne compatible = \"generic-uio\" oder ohne Interrupt?)\n", addr);
        exit(2);
    }
    char path[32];
    snprintf(path, sizeof path, "/dev/uio%d", n);
    int fd = open(path, O_RDWR);
    if (fd < 0) { perror(path); exit(2); }
    regs = mmap(NULL, MAP_BYTES, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) { perror("mmap"); exit(2); }
    return fd;
}

/* ---- Bildinhalt fuer Bild Nummer frame ---------------------------------------------- */

typedef struct { double x0, y0, x1, y1; } seg_t;

static int picture(uint32_t frame, seg_t s[LINES_N]) {
    double t = frame / 60.0;
    int n = 0;

    /* Wuerfel, dreht sich um zwei Achsen, mit Perspektive */
    static const int edges[12][2] = {{0,1},{1,3},{3,2},{2,0},{4,5},{5,7},{7,6},{6,4},{0,4},{1,5},{2,6},{3,7}};
    double px[8], py[8], a = 0.9 * t, b = 0.6 * t;
    for (int i = 0; i < 8; i++) {
        double x = (i & 1) ? 1 : -1, y = (i & 2) ? 1 : -1, z = (i & 4) ? 1 : -1;
        double x1 = x * cos(a) + z * sin(a), z1 = -x * sin(a) + z * cos(a);
        double y2 = y * cos(b) - z1 * sin(b), z2 = y * sin(b) + z1 * cos(b);
        double k = 1400.0 / (z2 + 5.0);
        px[i] = 960 + k * x1;
        py[i] = 470 + k * y2;
    }
    for (int i = 0; i < 12; i++)
        s[n++] = (seg_t){px[edges[i][0]], py[edges[i][0]], px[edges[i][1]], py[edges[i][1]]};

    /* Welle aus 16 Stuecken am unteren Rand */
    for (int i = 0; i < 16; i++) {
        double xa = 160 + i * 100.0, xb = xa + 100;
        s[n++] = (seg_t){xa, 960 + 70 * sin(xa / 160.0 - 3 * t), xb, 960 + 70 * sin(xb / 160.0 - 3 * t)};
    }

    /* Quadrat, das sich dreht und auch ueber den Bildrand hinaus wandert */
    double cx = 960 + 1150 * sin(0.37 * t), cy = 540 + 700 * sin(0.53 * t), r = 210, w = 0.8 * t;
    for (int i = 0; i < 4; i++) {
        double p = w + i * M_PI / 2, q = p + M_PI / 2;
        s[n++] = (seg_t){cx + r * cos(p), cy + r * sin(p), cx + r * cos(q), cy + r * sin(q)};
    }
    return n;
}

/* Linien von Bild frame in Puffer buf schreiben */
static void write_table(int buf, uint32_t frame) {
    seg_t s[LINES_N];
    int n = picture(frame, s);
    for (int i = 0; i < LINES_N; i++) {
        uint64_t w[2] = {0, 0};
        if (i < n) line_clip(s[i].x0, s[i].y0, s[i].x1, s[i].y1, w);
        unsigned off = REG_LINES_BASE + (buf * BUF_WORDS + 2 * i) * 8;
        wr(off,      (uint32_t)w[0]);
        wr(off + 4,  (uint32_t)(w[0] >> 32));
        wr(off + 8,  (uint32_t)w[1]);
        wr(off + 12, (uint32_t)(w[1] >> 32));
    }
}

int main(int argc, char **argv) {
    long count = 0, waste_ms = 0;
    int prio = 0, cpu = -1, quiet = 0, opt;
    while ((opt = getopt(argc, argv, "n:r:c:w:q")) != -1) {
        switch (opt) {
        case 'n': count = atol(optarg); break;
        case 'r': prio = atoi(optarg); break;
        case 'c': cpu = atoi(optarg); break;
        case 'w': waste_ms = atol(optarg); break;
        case 'q': quiet = 1; break;
        default:
            fprintf(stderr, "Aufruf: vec_demo [-n Bilder] [-r Prioritaet] [-c Kern] [-w ms] [-q]\n");
            return 2;
        }
    }

    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (sched_setaffinity(0, sizeof set, &set)) perror("sched_setaffinity");
    }
    if (prio > 0) {
        struct sched_param sp = { .sched_priority = prio };
        if (sched_setscheduler(0, SCHED_FIFO, &sp)) perror("sched_setscheduler");
    }
    mlockall(MCL_CURRENT | MCL_FUTURE);

    int fd = open_core();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);

    /* Erstes Bild: in den Puffer, der gerade nicht gewaehlt ist, dann Testbild 8 */
    uint32_t ctrl = rd(REG_LINES_CTRL) & 3;
    int buf = (ctrl & CTRL_TABLE) ? !(ctrl & CTRL_BUF1) : 0;
    write_table(buf, rd(REG_FRAMES) + 1);
    ctrl = CTRL_TABLE | (buf ? CTRL_BUF1 : 0);
    wr(REG_LINES_CTRL, ctrl);
    wr(REG_PATTERN, 8);

    long frames = 0, late = 0, skipped = 0, sec_frames = 0, sec_late = 0, sec_skipped = 0;
    double busy_max = 0, sec_busy_max = 0, busy_sum = 0, t_print = now() + 1;
    uint32_t prev = 0;
    int have_prev = 0;

    while (!stop && (count == 0 || frames < count)) {
        uint32_t one = 1, irqs;
        if (write(fd, &one, sizeof one) != sizeof one) { perror("write /dev/uio"); return 2; }
        if (read(fd, &irqs, sizeof irqs) != sizeof irqs) {
            if (errno == EINTR) continue;
            perror("read /dev/uio");
            return 2;
        }
        double t0 = now();
        uint32_t f = rd(REG_FRAMES), cur = rd(REG_LINES_CUR) & 3;

        if (have_prev) {
            if (f - prev > 1) { skipped += f - prev - 1; sec_skipped += f - prev - 1; }
            if (cur != ctrl) { late++; sec_late++; }
        }
        prev = f;
        have_prev = 1;

        /* Linien fuer das naechste Bild in den anderen Puffer, dann umschalten */
        buf = !buf;
        if (waste_ms) usleep(waste_ms * 1000);
        write_table(buf, f + 1);
        ctrl = CTRL_TABLE | (buf ? CTRL_BUF1 : 0);
        wr(REG_LINES_CTRL, ctrl);

        double busy = now() - t0;
        busy_sum += busy;
        if (busy > busy_max) busy_max = busy;
        if (busy > sec_busy_max) sec_busy_max = busy;
        frames++;
        sec_frames++;

        if (!quiet && t0 >= t_print) {
            printf("Bild %u: %ld Bilder/s, zu spaet %ld, ausgelassen %ld, Rechnen+Schreiben max. %.0f us\n",
                   f, sec_frames, sec_late, sec_skipped, sec_busy_max * 1e6);
            fflush(stdout);
            sec_frames = sec_late = sec_skipped = 0;
            sec_busy_max = 0;
            t_print += 1;
            if (t_print < t0) t_print = t0 + 1;
        }
    }

    printf("%ld Bilder, zu spaet %ld, ausgelassene Interrupts %ld, Rechnen+Schreiben im Mittel %.0f us, max. %.0f us\n",
           frames, late, skipped, frames ? busy_sum / frames * 1e6 : 0, busy_max * 1e6);
    return late ? 1 : 0;
}
