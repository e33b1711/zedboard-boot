/* =============================================================================
 *  I/Q-Daten vom HackRF empfangen und pruefen, ob der Datenstrom lueckenlos ist
 *
 *  Aufruf: iq_rx                      100 MHz, 10 Msps, 10 Sekunden, Daten verwerfen
 *          iq_rx -f 433.92e6 -s 8e6   Frequenz und Abtastrate in Hz
 *          iq_rx -t 60                Dauer in Sekunden (0 = bis Strg-C)
 *          iq_rx -l 24 -g 30 -a       LNA 0..40 dB (8er-Schritte), VGA 0..62 dB (2er-Schritte),
 *                                     -a schaltet den HF-Vorverstaerker ein
 *          iq_rx -o daten.iq          Daten in eine Datei schreiben ("-" = Standardausgabe)
 *          iq_rx -p                   Daten in die PL geben (Ringpuffer im DDR, Core iq_reader)
 *          iq_rx -z -s 16e6           Test ohne HackRF: Zaehlerdaten mit 16 Msps in die PL,
 *                                     der Core prueft sie auf Spruenge (-s 0 = so schnell es geht)
 *
 *  Daten:  8 Bit mit Vorzeichen, abwechselnd I und Q, also 2 Byte pro Abtastwert
 *          (10 Msps = 20 MB/s)
 *  Ausgabe je Sekunde: Datenrate, daraus die Abtastrate, Pegel (Effektivwert und Spitze,
 *          0 dBFS = Vollaussteuerung), CPU-Last des Programms (100 % = ein Kern) und die
 *          Zahl der Luecken, die das HackRF selbst zaehlt (sein Puffer war voll, weil der
 *          Rechner die Daten nicht schnell genug abgeholt hat).
 *  Weg in die PL: iq_rx schreibt die Bloecke ueber /dev/mem in den Ringpuffer
 *          (reserved-memory/iqring@... im Device Tree) und meldet dem Core iq_reader den
 *          Schreibzeiger. Ist der Ring voll, wird der Block verworfen und als Ueberlauf gezaehlt.
 *  Rueckgabe 0, wenn der Lauf lueckenlos war.
 * ============================================================================= */
#include <fcntl.h>
#include <getopt.h>
#include <glob.h>
#include <libhackrf/hackrf.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>

#define LEVEL_STEP 16               /* Pegel nur aus jedem 16. Abtastwert, spart Rechenzeit */
#define BLOCK_BYTES 262144          /* Blockgroesse von libhackrf, auch fuer die Zaehlerdaten */

/* Register des Cores iq_reader */
#define REG_ENABLE     0x10
#define REG_RING_ADDR  0x18
#define REG_RING_BYTES 0x20
#define REG_WR_PTR     0x28
#define REG_RD_PTR     0x30
#define REG_SAMPLES    0x40
#define REG_SEQ_ERRORS 0x50
#define CHUNK_BYTES    1024         /* der Core liest in Bloecken dieser Groesse */

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static uint64_t bytes_total;        /* alle empfangenen Bytes */
static uint64_t bytes_timed;        /* ohne den ersten Block, gehoert zu t_first..t_last */
static double t_first, t_last;      /* Zeit des ersten und des letzten Blocks */
static uint64_t level_sum, level_n; /* Summe der Betragsquadrate, Zahl der Werte */
static int level_peak;              /* groesster Betrag einer Komponente, 0..128 */
static FILE *out;
static int write_failed;
static volatile sig_atomic_t stop;

static double now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static double cpu_seconds(void) {
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    return ru.ru_utime.tv_sec + ru.ru_stime.tv_sec + (ru.ru_utime.tv_usec + ru.ru_stime.tv_usec) * 1e-6;
}

static void on_signal(int sig) { (void)sig; stop = 1; }

/* ---- Weg in die PL ---- */
static volatile uint32_t *pl_regs;  /* NULL = Daten nicht in die PL geben */
static uint8_t *ring;
static uint32_t ring_bytes, ring_wr;
static uint64_t pl_bytes;           /* in den Ring geschrieben */
static uint64_t pl_overflows;       /* verworfene Bloecke, weil der Ring voll war */

/* Erste Adresse und Groesse aus einer "reg"-Datei des Device Tree (je 32 Bit, Big Endian) */
static int read_reg(const char *pattern, uint32_t *addr, uint32_t *size) {
    glob_t g;
    char path[512];
    uint8_t b[8];
    if (glob(pattern, 0, NULL, &g)) return -1;
    snprintf(path, sizeof path, "%s/reg", g.gl_pathv[0]);
    globfree(&g);
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    size_t n = fread(b, 1, 8, f);
    fclose(f);
    if (n != 8) return -1;
    *addr = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    *size = (uint32_t)b[4] << 24 | b[5] << 16 | b[6] << 8 | b[7];
    return 0;
}

static void pl_open(void) {
    uint32_t ring_addr, base, size;
    if (read_reg("/proc/device-tree/reserved-memory/iqring@*", &ring_addr, &ring_bytes)
        || ring_bytes < 4 * BLOCK_BYTES || (ring_bytes & (ring_bytes - 1)) || ring_addr % CHUNK_BYTES) {
        fprintf(stderr, "FEHLER: reserved-memory/iqring@... fehlt im Device Tree oder passt nicht\n");
        exit(2);
    }
    if (read_reg("/proc/device-tree/*/iq_reader@*", &base, &size)) {
        fprintf(stderr, "FEHLER: Knoten iq_reader@... nicht im Device Tree (Bitstream ohne iq_reader?)\n");
        exit(2);
    }
    int fd = open("/dev/mem", O_RDWR | O_SYNC);
    if (fd < 0) { perror("/dev/mem"); exit(2); }
    pl_regs = mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, base);
    ring = mmap(NULL, ring_bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, ring_addr);
    if (pl_regs == MAP_FAILED || ring == MAP_FAILED) { perror("mmap"); exit(2); }

    /* anhalten, Ring eintragen, bei Schreibzeiger 0 neu beginnen */
    pl_regs[REG_ENABLE / 4] = 0;
    pl_regs[REG_RING_ADDR / 4] = ring_addr;
    pl_regs[REG_RING_BYTES / 4] = ring_bytes;
    pl_regs[REG_WR_PTR / 4] = ring_wr = 0;
    usleep(1000);
    pl_regs[REG_ENABLE / 4] = 1;
    fprintf(stderr, "Ringpuffer 0x%08X, %u MiB, Core iq_reader 0x%08X\n", ring_addr, ring_bytes >> 20, base);
}

static uint32_t pl_fill(void) { return (ring_wr - pl_regs[REG_RD_PTR / 4]) & (ring_bytes - 1); }

/* Block in den Ring schreiben; Rueckgabe 0, wenn dafuer kein Platz ist */
static int pl_push(const uint8_t *data, uint32_t n) {
    if (pl_fill() + n + CHUNK_BYTES > ring_bytes) return 0;
    uint32_t first = ring_bytes - ring_wr < n ? ring_bytes - ring_wr : n;
    memcpy(ring + ring_wr, data, first);
    memcpy(ring, data + first, n - first);
    ring_wr = (ring_wr + n) & (ring_bytes - 1);
    pl_regs[REG_WR_PTR / 4] = ring_wr;
    pl_bytes += n;
    return 1;
}

/* Warten, bis die PL alles gelesen hat, Zaehler holen, Core anhalten; Rueckgabe 1 = alles da */
static int pl_close(int counter_data) {
    for (int i = 0; i < 200 && pl_fill() >= CHUNK_BYTES; i++) usleep(5000);
    uint32_t samples = pl_regs[REG_SAMPLES / 4], jumps = pl_regs[REG_SEQ_ERRORS / 4];
    pl_regs[REG_ENABLE / 4] = 0;
    /* der Zaehler des Cores hat 32 Bit */
    int complete = samples == (uint32_t)(pl_bytes / 2);
    fprintf(stderr, "In die PL: %llu Abtastwerte geschrieben, der Core meldet %u (modulo 2^32)%s, Ring-Ueberlaeufe %llu\n",
            (unsigned long long)(pl_bytes / 2), samples, complete ? "" : " – STIMMT NICHT",
            (unsigned long long)pl_overflows);
    if (counter_data) fprintf(stderr, "Spruenge in den Zaehlerdaten laut Core: %u\n", jumps);
    return complete && pl_overflows == 0 && (!counter_data || jumps == 0);
}

static double dbfs(double amplitude) { return amplitude > 0 ? 20 * log10(amplitude / 128) : -99; }

/* Wird von libhackrf fuer jeden USB-Block aufgerufen (eigener Thread) */
static int rx_callback(hackrf_transfer *t) {
    const int8_t *s = (const int8_t *)t->buffer;
    int n = t->valid_length;
    double time = now();
    uint64_t sum = 0;
    int peak = 0;

    for (int i = 0; i + 1 < n; i += 2 * LEVEL_STEP) {
        int a = s[i], b = s[i + 1];
        sum += a * a + b * b;
        if (abs(a) > peak) peak = abs(a);
        if (abs(b) > peak) peak = abs(b);
    }
    if (out && !write_failed && fwrite(s, 1, n, out) != (size_t)n) write_failed = 1;
    if (pl_regs && !pl_push(t->buffer, n)) pl_overflows++;

    pthread_mutex_lock(&lock);
    if (bytes_total == 0) t_first = time; else bytes_timed += n;
    t_last = time;
    bytes_total += n;
    level_sum += sum;
    level_n += (n / 2 + LEVEL_STEP - 1) / LEVEL_STEP;
    if (peak > level_peak) level_peak = peak;
    pthread_mutex_unlock(&lock);
    return stop || write_failed;
}

static void check(int result, const char *what) {
    if (result == HACKRF_SUCCESS) return;
    fprintf(stderr, "FEHLER: %s: %s\n", what, hackrf_error_name(result));
    exit(2);
}

static void usage(void) {
    fprintf(stderr,
        "Aufruf: iq_rx [-f Hz] [-s Hz] [-t Sekunden] [-l LNA-dB] [-g VGA-dB] [-a] [-o Datei] [-p]\n"
        "        iq_rx -z [-s Hz] [-t Sekunden]\n"
        "        Vorgabe: -f 100e6 -s 10e6 -t 10 -l 16 -g 20, Daten verwerfen\n"
        "        -p = Daten in die PL, -z = Zaehlerdaten statt HackRF in die PL (-s 0 = ungebremst)\n");
    exit(2);
}

static double number(const char *arg) {
    char *end;
    double v = strtod(arg, &end);
    if (end == arg || *end || v < 0) usage();
    return v;
}

/* Test ohne HackRF: Bloecke mit fortlaufenden 16-Bit-Zaehlerwerten in die PL schreiben.
   Ist der Ring voll, wird gewartet statt verworfen; gemessen wird also, was die Strecke schafft. */
static int counter_test(double rate, double duration) {
    static uint16_t block[BLOCK_BYTES / 2];
    uint16_t value = 0;
    double start = now(), time_last = start, cpu_last = cpu_seconds(), waited = 0;
    uint64_t bytes_last = 0;
    int second = 1;

    pl_open();
    if (rate > 0) fprintf(stderr, "Zaehlerdaten mit %.3f Msps = %.2f MB/s\n", rate / 1e6, 2 * rate / 1e6);
    else          fprintf(stderr, "Zaehlerdaten ungebremst\n");
    while (!stop) {
        for (int i = 0; i < BLOCK_BYTES / 2; i++) block[i] = value++;
        while (!stop && !pl_push((const uint8_t *)block, BLOCK_BYTES)) {
            usleep(200);
            waited += 200e-6;
        }
        /* Solltempo einhalten */
        while (!stop && rate > 0 && now() < start + pl_bytes / (2 * rate)) usleep(200);

        double time = now();
        if (time >= start + second) {
            double cpu = cpu_seconds(), mb = (pl_bytes - bytes_last) / (time - time_last) / 1e6;
            fprintf(stderr, "%4d s  %6.2f MB/s  %6.3f Msps  CPU %3.0f %%  Ring %3.0f %% voll  Spruenge %u\n",
                    second, mb, mb / 2, 100 * (cpu - cpu_last) / (time - time_last),
                    100.0 * pl_fill() / ring_bytes, (unsigned)pl_regs[REG_SEQ_ERRORS / 4]);
            bytes_last = pl_bytes;
            time_last = time;
            cpu_last = cpu;
            if (duration > 0 && second >= duration) break;
            second++;
        }
    }
    double span = now() - start;
    fprintf(stderr, "Geschrieben: %llu Byte in %.1f s = %.3f Msps, davon %.1f s auf Platz im Ring gewartet\n",
            (unsigned long long)pl_bytes, span, pl_bytes / span / 2e6, waited);
    int ok = pl_close(1);
    fprintf(stderr, "ERGEBNIS: %s\n", ok ? "lueckenlos" : "NICHT lueckenlos");
    return ok ? 0 : 1;
}

int main(int argc, char **argv) {
    double freq = 100e6, rate = 10e6, duration = 10;
    int lna = 16, vga = 20, amp = 0, to_pl = 0, counter = 0, opt;
    const char *out_name = NULL;

    while ((opt = getopt(argc, argv, "f:s:t:l:g:ao:pz")) != -1) {
        switch (opt) {
        case 'f': freq = number(optarg); break;
        case 's': rate = number(optarg); break;
        case 't': duration = number(optarg); break;
        case 'l': lna = (int)number(optarg); break;
        case 'g': vga = (int)number(optarg); break;
        case 'a': amp = 1; break;
        case 'o': out_name = optarg; break;
        case 'p': to_pl = 1; break;
        case 'z': counter = 1; break;
        default:  usage();
        }
    }
    if (optind != argc) usage();
    signal(SIGINT, on_signal);
    signal(SIGTERM, on_signal);
    signal(SIGPIPE, on_signal);
    if (counter) {
        if (rate != 0 && (rate < 1e5 || rate > 100e6)) usage();
        return counter_test(rate, duration);
    }
    if (rate < 2e6 || rate > 20e6) usage();

    if (out_name) {
        out = strcmp(out_name, "-") ? fopen(out_name, "wb") : stdout;
        if (!out) { perror(out_name); return 2; }
    }

    hackrf_device *dev = NULL;
    uint8_t board_id = 0;
    char version[128] = "?";
    check(hackrf_init(), "hackrf_init");
    if (hackrf_open(&dev) != HACKRF_SUCCESS) {
        fprintf(stderr, "FEHLER: kein HackRF gefunden (USB-Kabel? 5 V auf VBUS? PortaPack im HackRF-Modus?)\n");
        return 2;
    }
    hackrf_board_id_read(dev, &board_id);
    hackrf_version_string_read(dev, version, sizeof version - 1);
    fprintf(stderr, "%s, Firmware %s\n", hackrf_board_id_name(board_id), version);

    check(hackrf_set_sample_rate(dev, rate), "Abtastrate");
    check(hackrf_set_freq(dev, (uint64_t)freq), "Frequenz");
    check(hackrf_set_amp_enable(dev, amp), "HF-Vorverstaerker");
    check(hackrf_set_lna_gain(dev, lna), "LNA");
    check(hackrf_set_vga_gain(dev, vga), "VGA");
    fprintf(stderr, "%.6f MHz, %.3f Msps = %.2f MB/s, LNA %d dB, VGA %d dB, Vorverstaerker %s\n",
            freq / 1e6, rate / 1e6, 2 * rate / 1e6, lna, vga, amp ? "ein" : "aus");

    if (to_pl) pl_open();
    check(hackrf_start_rx(dev, rx_callback, NULL), "Empfang starten");

    /* Zaehler des HackRF fuer Luecken; aeltere Firmware kennt ihn nicht */
    hackrf_m0_state m0;
    int have_m0 = 0;
    uint32_t gaps = 0, longest = 0;

    double start = now(), cpu_last = cpu_seconds(), time_last = start;
    uint64_t bytes_last = 0;
    for (int second = 1; !stop && hackrf_is_streaming(dev) == HACKRF_TRUE; second++) {
        while (!stop && now() < start + second) usleep(20000);
        if (stop) break;

        pthread_mutex_lock(&lock);
        uint64_t bytes = bytes_total, sum = level_sum, n = level_n;
        int peak = level_peak;
        level_sum = level_n = 0;
        level_peak = 0;
        pthread_mutex_unlock(&lock);

        if (hackrf_get_m0_state(dev, &m0) == HACKRF_SUCCESS) {
            have_m0 = 1;
            gaps = m0.num_shortfalls;
            longest = m0.longest_shortfall;
        }
        double time = now(), cpu = cpu_seconds();
        double mb = (bytes - bytes_last) / (time - time_last) / 1e6;
        fprintf(stderr, "%4d s  %6.2f MB/s  %6.3f Msps  Pegel %5.1f dBFS  Spitze %5.1f dBFS  CPU %3.0f %%  Luecken ",
                second, mb, mb / 2, dbfs(n ? sqrt((double)sum / n / 2) : 0), dbfs(peak),
                100 * (cpu - cpu_last) / (time - time_last));
        if (have_m0) fprintf(stderr, "%u", gaps); else fprintf(stderr, "?");
        if (pl_regs) fprintf(stderr, "  Ring %3.0f %% voll, Ueberlaeufe %llu", 100.0 * pl_fill() / ring_bytes,
                             (unsigned long long)pl_overflows);
        fprintf(stderr, "\n");
        bytes_last = bytes;
        time_last = time;
        cpu_last = cpu;
        if (duration > 0 && second >= duration) break;
    }
    /* Zaehler vor dem Anhalten lesen: beim Anhalten holt der Rechner nichts mehr ab,
       das HackRF tastet noch kurz weiter und zaehlt das als Luecken */
    if (hackrf_get_m0_state(dev, &m0) == HACKRF_SUCCESS) {
        have_m0 = 1;
        gaps = m0.num_shortfalls;
        longest = m0.longest_shortfall;
    }
    stop = 1;
    hackrf_stop_rx(dev);
    hackrf_close(dev);
    hackrf_exit();
    if (out && (fflush(out) || write_failed)) {
        fprintf(stderr, "FEHLER: Schreiben nach %s fehlgeschlagen\n", out_name);
        return 2;
    }

    /* Abtastrate aus der Zeit zwischen erstem und letztem Block; die Bloecke kommen
       stossweise, deshalb ist das erst ueber einige Sekunden genau */
    double span = t_last - t_first;
    double measured = span > 0 ? bytes_timed / span / 2 : 0;
    double deviation = 100 * (measured - rate) / rate;
    fprintf(stderr, "Empfangen: %llu Byte, %.4f Msps gemessen, Soll %.4f Msps (%+.2f %%)\n",
            (unsigned long long)bytes_total, measured / 1e6, rate / 1e6, deviation);

    int ok, pl_ok = pl_regs ? pl_close(0) : 1;
    if (bytes_total == 0) {
        fprintf(stderr, "ERGEBNIS: keine Daten empfangen\n");
        ok = 0;
    } else if (have_m0) {
        fprintf(stderr, "Luecken laut HackRF: %u, laengste %u Byte\n", gaps, longest);
        ok = gaps == 0 && pl_ok;
        fprintf(stderr, "ERGEBNIS: %s\n", ok ? "lueckenlos" : "NICHT lueckenlos");
    } else {
        /* ohne Zaehler bleibt nur die Datenrate: fehlende Daten machen sie zu klein */
        ok = deviation > -1 && pl_ok;
        fprintf(stderr, "Firmware ohne Lueckenzaehler, Urteil nur aus der Datenrate\n");
        fprintf(stderr, "ERGEBNIS: %s\n", ok ? "Datenrate stimmt, vermutlich lueckenlos" : "Daten fehlen");
    }
    return ok ? 0 : 1;
}
