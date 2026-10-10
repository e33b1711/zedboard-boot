// C-Testbench: speist Traeger und Rauschen ein und prueft Lage und Hoehe der Linien,
// die Blockmittelung (ein Spektrum je 2^avg_log2 Bloecke, ruhigerer Rauschboden) und
// den Max-Hold (haelt einen verschwundenen Traeger, laesst sich loeschen).
#include "iq_spectrum.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>

static hls::stream<ap_uint<16> > in;
static hls::stream<spectrum_t> out;
static ap_uint<32> spectra, overflows;
static int errors = 0;
static double phase = 0;

static void check(bool ok, const char *what) {
    if (!ok) { errors++; printf("FEHLER: %s\n", what); }
}

static int clip(double v) { int n = (int)lround(v); return n < -128 ? -128 : n > 127 ? 127 : n; }

// Bloecke mit einem Traeger auf Linie "bin" (negativ = unter der Mitte) und Rauschen
static void run(int blocks, int avg_log2, int maxhold, double amplitude, double bin, double noise) {
    for (int b = 0; b < blocks; b++) {
        for (int i = 0; i < FFT_LEN; i++) {
            phase += 2 * M_PI * bin / FFT_LEN;
            double n1 = noise * (rand() / (double)RAND_MAX - 0.5), n2 = noise * (rand() / (double)RAND_MAX - 0.5);
            int re = clip(amplitude * cos(phase) + n1), im = clip(amplitude * sin(phase) + n2);
            in.write(((im & 0xFF) << 8) | (re & 0xFF));
        }
        iq_spectrum(avg_log2, maxhold, &spectra, &overflows, in, out);
    }
}

// Ein Spektrum abholen; Rueckgabe false, wenn keins da ist
static bool take(int now[FFT_LEN], int peak[FFT_LEN]) {
    if (out.empty()) return false;
    for (int i = 0; i < FFT_LEN; i++) {
        spectrum_t v = out.read();
        now[i] = v.data & 0xFF;
        peak[i] = (v.data >> 8) & 0xFF;
        if ((v.last == 1) != (i == FFT_LEN - 1)) { errors++; printf("FEHLER: tlast bei Linie %d\n", i); }
    }
    return true;
}

static int argmax(const int v[FFT_LEN]) {
    int m = 0;
    for (int i = 1; i < FFT_LEN; i++) if (v[i] > v[m]) m = i;
    return m;
}

int main() {
    static int now[FFT_LEN], peak[FFT_LEN];

    // 1. Traeger mit voller Amplitude auf Linie 100, ohne Mittelung: ein Spektrum je Block
    run(1, 0, 0, 127, 100, 0);
    check(take(now, peak), "Spektrum nach einem Block");
    printf("voller Traeger:  Linie %d, Pegel %d, Nachbarn %d %d, weit weg %d\n",
           argmax(now), now[100], now[99], now[101], now[300]);
    check(argmax(now) == 100 && now[100] >= 253, "voller Traeger steht auf Linie 100 bei 255");
    check(now[99] >= now[100] - 20 && now[99] <= now[100] - 12, "Nachbarlinie 6 dB darunter (Hann-Fenster)");
    check(now[300] < 60, "weit neben dem Traeger ist es leer");

    // 2. Traeger 40 dB schwaecher unterhalb der Mitte: Linie 1024 - 200, 106 Stufen tiefer
    phase = 0;
    run(1, 0, 0, 1.27, -200, 0);           // Amplitude 1,27 wird zu +-1 gerundet: grob 40 dB
    run(1, 0, 0, 12.7, -200, 0);
    take(now, peak);
    take(now, peak);
    printf("Traeger -20 dB:  Linie %d, Pegel %d\n", argmax(now), now[824]);
    check(argmax(now) == 824 && abs(now[824] - (255 - 53)) <= 3, "Traeger mit -20 dB auf Linie 824, 53 Stufen tiefer");

    // 3. Mittelung ueber 16 Bloecke: genau ein Spektrum, der Rauschboden wird ruhiger
    phase = 0;
    run(1, 0, 0, 0, 0, 40);
    take(now, peak);
    int lo1 = 255, hi1 = 0;
    for (int i = 0; i < FFT_LEN; i++) { if (now[i] < lo1) lo1 = now[i]; if (now[i] > hi1) hi1 = now[i]; }
    run(15, 4, 0, 0, 0, 40);
    check(out.empty(), "kein Spektrum vor dem 16. Block");
    run(1, 4, 0, 0, 0, 40);
    check(take(now, peak) && out.empty(), "genau ein Spektrum nach 16 Bloecken");
    int lo16 = 255, hi16 = 0;
    for (int i = 0; i < FFT_LEN; i++) { if (now[i] < lo16) lo16 = now[i]; if (now[i] > hi16) hi16 = now[i]; }
    printf("Rauschen:        ohne Mittelung %d..%d, ueber 16 Bloecke %d..%d\n", lo1, hi1, lo16, hi16);
    check(hi16 - lo16 < (hi1 - lo1) / 2, "Mittelung macht den Rauschboden ruhiger");
    check(abs((lo16 + hi16) / 2 - hi1) < 40 && hi16 <= hi1 + 2, "Mittelwert liegt im Bereich der Einzelwerte");

    // 4. Max-Hold: Traeger zeigen, wegnehmen, er bleibt stehen; mit maxhold = 0 verschwindet er
    phase = 0;
    run(4, 2, 1, 100, 50, 4);
    take(now, peak);
    int seen = now[50];
    run(4, 2, 1, 0, 0, 4);
    take(now, peak);
    printf("Max-Hold:        Traeger %d, danach Pegel %d, gehalten %d\n", seen, now[50], peak[50]);
    check(now[50] < seen - 60 && peak[50] == seen, "Max-Hold haelt den verschwundenen Traeger");
    for (int i = 0; i < FFT_LEN; i++) if (peak[i] < now[i]) { check(false, "Max-Hold unter dem Pegel"); break; }
    run(4, 2, 0, 0, 0, 4);
    take(now, peak);
    check(peak[50] == now[50], "maxhold = 0 loescht");

    check(overflows == 0, "kein Ueberlauf in der FFT");
    check(spectra == 8, "Zaehler der Spektren");
    check(in.empty() && out.empty(), "nichts uebrig");

    if (errors) { printf("TEST FEHLGESCHLAGEN: %d Fehler\n", errors); return 1; }
    printf("TEST OK\n");
    return 0;
}
