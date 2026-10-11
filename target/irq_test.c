/* =============================================================================
 *  Laufzeit eines Interrupts von der PL bis zur Antwort des Programms messen
 *
 *  Aufruf: irq_test                   10000 Messungen, Pause 1 ms plus bis zu 0,66 ms Zufall
 *          irq_test -n 100000         Zahl der Messungen (0 = bis Strg-C)
 *          irq_test -i 500 -j 0       Pause zwischen zwei Messungen in Mikrosekunden und
 *                                     groesster zufaelliger Zuschlag dazu (abgerundet auf 2^k - 1 Takte)
 *          irq_test -r 80             mit Echtzeit-Prioritaet (SCHED_FIFO, 1..99)
 *          irq_test -c 1              an einen Prozessorkern binden
 *          irq_test -H                Verteilung der Laufzeiten ausgeben
 *          irq_test -b                Vergleich ohne Interrupt: das Programm fragt das
 *                                     Statusregister in einer Schleife ab
 *          irq_test -d                Mittelwert und groesste Laufzeit auch auf der
 *                                     OLED-Anzeige zeigen (-u = Bild um 180 Grad gedreht)
 *
 *  Ablauf: der Core irq_timer (rtl/irq_timer.v) setzt den IRQ und merkt sich den Stand
 *          seines Zeitzaehlers. Linux weckt dieses Programm (UIO, read auf /dev/uioN), das
 *          Programm schreibt ACK in das Statusregister. Mit diesem Schreibzugriff haelt der
 *          Core die Zeit an. Gemessen wird also die ganze Strecke: Interrupt-Controller,
 *          Kernel, Wechsel in das Programm, Schreibzugriff ueber AXI-Lite.
 *  Statistik: Anzahl, Summe, Quadratsumme, Minimum und Maximum fuehrt der Core in Takten
 *          (10 ns); hier werden daraus Mittelwert, Jitter (Standardabweichung) und der
 *          schlechteste Fall. Median und Verteilung zaehlt das Programm aus den einzelnen
 *          Laufzeiten (Register LAST), mit 1 us Aufloesung; daher kennt es auch die Nummer
 *          der Messung mit der groessten Laufzeit (die erste hat die Nummer 1).
 *  Treiber: uio_pdrv_genirq mit of_id=generic-uio (steht in /etc/modprobe.d/uio.conf);
 *          fehlt das UIO-Geraet, laedt das Programm das Modul selbst neu.
 *  Vorlauf: vor der Messung laeuft ein Durchgang, der nicht zaehlt (der erste Interrupt
 *          nach dem Start ist regelmaessig langsamer als die folgenden).
 *  Ausgabe: einmal pro Sekunde der Zwischenstand, mit -d auch auf der OLED-Anzeige
 *          (oled.h). Dafuer haelt das Programm die Messung kurz an, damit kein Interrupt
 *          in die Ausgabe faellt und zu lang gemessen wird.
 *  Rueckgabe 0, wenn gemessen wurde.
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

#include "oled.h"

/* Register des Cores irq_timer, als Index in 32-Bit-Worten */
enum { CTRL, STATUS, INTERVAL, RAND_MASK, LIMIT, COUNT, LAST, MIN, MAX,
       SUM_LO, SUM_HI, SQ_LO, SQ_MID, SQ_HI, NOW, CLK_HZ };
#define CTRL_ENABLE 1
#define CTRL_CLEAR  2
#define STATUS_IRQ  1
#define STATUS_LATE 4

#define HIST_BINS 10000             /* je 1 us, darueber ein Sammelfach */

static volatile uint32_t *regs;
static volatile sig_atomic_t stop;
static uint32_t hist[HIST_BINS + 1];

static void on_signal(int sig) { (void)sig; stop = 1; }

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

/* Basisadresse des Cores aus dem Device Tree */
static int core_addr(uint32_t *addr) {
    glob_t g;
    if (glob("/proc/device-tree/*/irq_timer@*/reg", 0, NULL, &g) != 0) return -1;
    FILE *f = fopen(g.gl_pathv[0], "rb");
    globfree(&g);
    uint8_t b[4];
    if (!f || fread(b, 1, 4, f) != 4) return -1;
    fclose(f);
    *addr = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    return 0;
}

/* Nummer des UIO-Geraets, dessen Speicherbereich bei addr beginnt; -1 = keines */
static int uio_number(uint32_t addr) {
    glob_t g;
    int found = -1;
    if (glob("/sys/class/uio/uio*/maps/map0/addr", 0, NULL, &g) != 0) return -1;
    for (size_t i = 0; i < g.gl_pathc && found < 0; i++) {
        FILE *f = fopen(g.gl_pathv[i], "r");
        unsigned long a = 0;
        if (f && fscanf(f, "%lx", &a) == 1 && a == addr)
            sscanf(g.gl_pathv[i], "/sys/class/uio/uio%d", &found);
        if (f) fclose(f);
    }
    globfree(&g);
    return found;
}

static int core_open(void) {
    uint32_t addr;
    if (core_addr(&addr)) {
        fprintf(stderr, "FEHLER: Knoten irq_timer@... nicht im Device Tree (Bitstream ohne irq_timer?)\n");
        exit(2);
    }
    int n = uio_number(addr);
    if (n < 0) {
        /* udev laedt das Modul beim Start schon selbst, aber ohne of_id: dann neu laden */
        if (system("rmmod uio_pdrv_genirq 2>/dev/null; modprobe uio_pdrv_genirq of_id=generic-uio") != 0)
            fprintf(stderr, "WARN: modprobe uio_pdrv_genirq fehlgeschlagen\n");
        for (int i = 0; i < 20 && n < 0; i++) {     /* udev legt /dev/uioN erst an */
            usleep(100000);
            n = uio_number(addr);
        }
    }
    if (n < 0) {
        fprintf(stderr, "FEHLER: kein UIO-Geraet fuer irq_timer@%08x "
                "(Knoten ohne compatible = \"generic-uio\" oder ohne Interrupt?)\n", addr);
        exit(2);
    }
    char path[32];
    snprintf(path, sizeof path, "/dev/uio%d", n);
    int fd = open(path, O_RDWR);
    for (int i = 0; i < 20 && fd < 0; i++) {
        usleep(100000);
        fd = open(path, O_RDWR);
    }
    if (fd < 0) { perror(path); exit(2); }
    regs = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (regs == MAP_FAILED) { perror("mmap"); exit(2); }
    return fd;
}

/* Statistik des Cores in Mikrosekunden */
struct stats { uint32_t n; double mean, sigma, min, max; };

static struct stats read_stats(double us) {
    struct stats s = { .n = regs[COUNT] };
    if (s.n == 0) return s;
    double sum = regs[SUM_HI] * 4294967296.0 + regs[SUM_LO];
    double sq = (regs[SQ_HI] * 4294967296.0 + regs[SQ_MID]) * 4294967296.0 + regs[SQ_LO];
    double mean = sum / s.n, var = sq / s.n - mean * mean;
    s.mean = mean * us;
    s.sigma = var > 0 ? sqrt(var) * us : 0;
    s.min = regs[MIN] * us;
    s.max = regs[MAX] * us;
    return s;
}

/* Laufzeit in us, unter der der Anteil p der Messungen liegt */
static int percentile(uint32_t n, double p) {
    uint64_t want = (uint64_t)ceil(n * p), seen = 0;
    for (int i = 0; i <= HIST_BINS; i++) {
        seen += hist[i];
        if (seen >= want) return i;
    }
    return HIST_BINS;
}

static void print_hist(void) {
    int first = 0, last = HIST_BINS;
    uint32_t peak = 1;
    while (first < HIST_BINS && !hist[first]) first++;
    while (last > first && !hist[last]) last--;
    for (int i = first; i <= last; i++) if (hist[i] > peak) peak = hist[i];
    printf("\nVerteilung (Laufzeit in us, Anzahl; die Balken sind logarithmisch):\n");
    for (int i = first, gap = 0; i <= last; i++) {
        if (!hist[i]) { gap++; continue; }
        if (gap) printf("     ...\n");
        gap = 0;
        int bar = 1 + (int)(49 * log(hist[i]) / log(peak > 1 ? peak : 2));
        printf("%s%5d %9u  %.*s\n", i == HIST_BINS ? ">=" : "  ", i, hist[i], bar,
               "##################################################");
    }
}

/* Mittelwert und groesste Laufzeit mit der Nummer ihrer Messung auf die OLED-Anzeige */
static void show_oled(struct stats s, uint32_t worst_index) {
    char mean[24], worst[32];
    snprintf(mean, sizeof mean, "mean %8.1f us", s.mean);
    if (snprintf(worst, sizeof worst, "worst %.0fus #%u", s.max, worst_index) > 16)
        snprintf(worst, sizeof worst, "w %.0fus #%u", s.max, worst_index);
    oled_text(mean, worst);
}

/* Einen Interrupt abwarten und quittieren. Rueckgabe 1, 0 bei Abbruch durch ein Signal,
 * -1 bei einem Fehler. */
static int round_trip(int fd, int busy) {
    const uint32_t one = 1;
    uint32_t events;
    if (busy) {
        while (!(regs[STATUS] & STATUS_IRQ) && !stop) ;
        if (stop) return 0;
        regs[STATUS] = 1;                                   /* Stopp der Messung */
        return 1;
    }
    if (read(fd, &events, sizeof events) != sizeof events) {
        if (errno == EINTR) return 0;
        perror("read /dev/uio");
        return -1;
    }
    regs[STATUS] = 1;                                       /* Stopp der Messung */
    /* der Treiber sperrt den IRQ bei jedem Auftreten: wieder freigeben */
    if (write(fd, &one, sizeof one) != sizeof one) { perror("IRQ freigeben"); return -1; }
    return 1;
}

static void usage(void) {
    fprintf(stderr,
        "Aufruf: irq_test [-n anzahl] [-i pause_us] [-j zufall_us] [-r prioritaet] [-c kern] [-H] [-b] [-d] [-u]\n"
        "  -n  Zahl der Messungen, 0 = bis Strg-C (Vorgabe 10000)\n"
        "  -i  Pause zwischen zwei Messungen in us (Vorgabe 1000)\n"
        "  -j  groesster zufaelliger Zuschlag zur Pause in us (Vorgabe 1000)\n"
        "  -r  Echtzeit-Prioritaet SCHED_FIFO 1..99 (Vorgabe: normale Prioritaet)\n"
        "  -c  an diesen Prozessorkern binden\n"
        "  -H  Verteilung der Laufzeiten ausgeben\n"
        "  -b  ohne Interrupt: Statusregister in einer Schleife abfragen\n"
        "  -d  Mittelwert und groesste Laufzeit auf der OLED-Anzeige zeigen\n"
        "  -u  wie -d, Bild um 180 Grad gedreht\n");
    exit(1);
}

static double number(const char *arg, double max) {
    char *end;
    double v = strtod(arg, &end);
    if (end == arg || *end || v < 0 || v > max) usage();
    return v;
}

int main(int argc, char **argv) {
    uint32_t count = 10000;
    double pause_us = 1000, rand_us = 1000;
    int prio = 0, cpu = -1, show_hist = 0, busy = 0, display = 0, flip = 0, opt;

    while ((opt = getopt(argc, argv, "n:i:j:r:c:Hbdu")) != -1) {
        switch (opt) {
        case 'n': count = (uint32_t)number(optarg, 4e9); break;
        case 'i': pause_us = number(optarg, 10e6); break;
        case 'j': rand_us = number(optarg, 10e6); break;
        case 'r': prio = (int)number(optarg, 99); break;
        case 'c': cpu = (int)number(optarg, 63); break;
        case 'H': show_hist = 1; break;
        case 'b': busy = 1; break;
        case 'd': display = 1; break;
        case 'u': display = flip = 1; break;
        default:  usage();
        }
    }
    if (optind != argc) usage();

    int fd = core_open();
    if (display && (oled_open() || oled_init(flip) || oled_text("irq_test", "..."))) return 2;
    double clk_hz = regs[CLK_HZ], us = 1e6 / clk_hz;

    /* Zufall: Maske 2^k - 1, die groesste, die den Zuschlag nicht ueberschreitet */
    uint32_t mask = 0;
    while (((uint64_t)mask << 1 | 1) <= rand_us / us) mask = mask << 1 | 1;

    if (cpu >= 0) {
        cpu_set_t set;
        CPU_ZERO(&set);
        CPU_SET(cpu, &set);
        if (sched_setaffinity(0, sizeof set, &set)) { perror("sched_setaffinity"); return 2; }
    }
    if (prio > 0) {
        struct sched_param sp = { .sched_priority = prio };
        if (sched_setscheduler(0, SCHED_FIFO, &sp)) { perror("sched_setscheduler"); return 2; }
    }
    mlockall(MCL_CURRENT | MCL_FUTURE);

    struct sigaction sa = { .sa_handler = on_signal };      /* ohne SA_RESTART: read bricht ab */
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGALRM, &sa, NULL);

    printf("%s, Pause %.0f us + 0..%.0f us Zufall, %s, Zeittakt %.0f ns\n",
           busy ? "Abfrage ohne Interrupt" : "Interrupt ueber UIO",
           pause_us, mask * us, prio ? "SCHED_FIFO" : "normale Prioritaet", us * 1e3);
    fflush(stdout);

    const uint32_t one = 1;
    regs[CTRL] = 0;
    regs[CTRL] = CTRL_CLEAR;
    regs[INTERVAL] = (uint32_t)(pause_us / us);
    regs[RAND_MASK] = mask;
    if (!busy && write(fd, &one, sizeof one) != sizeof one) { perror("IRQ einschalten"); return 2; }

    /* Ein Durchgang vorab, der nicht zaehlt: der erste Interrupt nach dem Start braucht
     * regelmaessig etwa doppelt so lange wie die folgenden. Bis er da ist, laeuft eine
     * Ueberwachung; danach stoert kein Signal die Messung. */
    regs[LIMIT] = 1;
    regs[CTRL] = CTRL_ENABLE;
    if (!busy) alarm((unsigned)((pause_us + rand_us) / 1e6) + 3);
    int warm = round_trip(fd, busy);
    alarm(0);
    if (warm < 0) return 2;
    if (warm == 0) {
        regs[CTRL] = 0;
        fprintf(stderr, "FEHLER: kein Interrupt angekommen (cat /proc/interrupts, Zeile irq_timer)\n");
        return 2;
    }
    regs[CTRL] = 0;
    regs[CTRL] = CTRL_CLEAR;
    regs[LIMIT] = count;
    regs[CTRL] = CTRL_ENABLE;

    uint32_t done = 0;
    uint32_t worst = 0, worst_index = 0;    /* groesste Laufzeit in Takten, Nummer ihrer Messung ab 1 */
    double t_report = now() + 1;
    while (!stop && (count == 0 || done < count)) {
        int ok = round_trip(fd, busy);
        if (ok < 0) return 2;
        if (ok == 0) break;
        done++;

        uint32_t last = regs[LAST];
        if (last > worst) { worst = last; worst_index = done; }
        uint32_t bin = (uint32_t)(last * us);
        hist[bin < HIST_BINS ? bin : HIST_BINS]++;

        double t = now();
        if (t >= t_report) {
            regs[CTRL] = 0;                                 /* kein IRQ waehrend der Ausgabe */
            struct stats s = read_stats(us);
            fprintf(stderr, "%10u Messungen: Mittel %8.2f us, Jitter %7.2f us, groesste %9.2f us (Nr. %u)\n",
                    s.n, s.mean, s.sigma, s.max, worst_index);
            if (display) show_oled(s, worst_index);
            t_report = now() + 1;
            regs[CTRL] = CTRL_ENABLE;
        }
    }
    int late = regs[STATUS] & STATUS_LATE;
    regs[CTRL] = 0;

    if (done == 0) {
        fprintf(stderr, "abgebrochen, keine Messung\n");
        return 2;
    }

    struct stats s = read_stats(us);
    if (display) show_oled(s, worst_index);
    printf("\nMessungen:           %u\n", s.n);
    printf("Mittelwert:          %9.2f us\n", s.mean);
    printf("Jitter:              %9.2f us  (Standardabweichung)\n", s.sigma);
    printf("Spanne:              %9.2f us  (groesste - kleinste)\n", s.max - s.min);
    printf("kleinste Laufzeit:   %9.2f us\n", s.min);
    printf("groesste Laufzeit:   %9.2f us  (schlechtester Fall, Messung Nr. %u von %u)\n", s.max, worst_index, done);
    printf("Median %d us, 99 %% unter %d us, 99,9 %% unter %d us\n",
           percentile(done, 0.5), percentile(done, 0.99) + 1, percentile(done, 0.999) + 1);
    if (late) printf("ACHTUNG: mindestens eine Laufzeit war laenger als der Zaehlbereich (%.0f s)\n",
                     4294967295.0 / clk_hz);
    if (s.n != done) printf("ACHTUNG: der Core hat %u Messungen gezaehlt, das Programm %u\n", s.n, done);
    if (show_hist) print_hist();
    return 0;
}
