// C-Testbench: prueft Stille, Amplitude und Frequenz des Sinus-Generators
#include "sine_gen.h"
#include <cmath>
#include <cstdio>

static const double FS = 48000.0;

static int check_tone(double freq) {
    const int N = 48000;                              // 1 s
    ap_uint<32> inc = (ap_uint<32>)std::llround(freq * 4294967296.0 / FS);
    hls::stream<sample_t> s;
    int crossings = 0, peak = 0, prev = 0;
    double err_max = 0.0;
    for (int n = 0; n < N; n++) {
        sine_gen(inc, s);
        int v = s.read().to_int();
        if (std::abs(v) > peak) peak = std::abs(v);
        if (n > 0 && prev < 0 && v >= 0) crossings++;
        prev = v;
        // Referenz: Phase vor dem Inkrement, Mitte des Tabellenschritts
        double ph  = (double)(ap_uint<32>)(inc * (ap_uint<32>)n) / 4294967296.0;
        double ref = AMPLITUDE * std::sin(2.0 * M_PI * ph);
        double e   = std::fabs(v - ref) / AMPLITUDE;
        if (e > err_max) err_max = e;
    }
    // Nach einem Ton wieder Stille, damit der naechste Test bei Phase 0 beginnt
    sine_gen(0, s); s.read();
    double f_meas = crossings * FS / N;
    std::printf("f=%8.1f Hz  inc=0x%08x  gemessen=%8.1f Hz  peak=%d  max.Fehler=%.5f\n",
                freq, (unsigned)inc, f_meas, peak, err_max);
    int fail = 0;
    if (std::fabs(f_meas - freq) > 1.5)              fail = 1;   // +-1 Nulldurchgang
    if (peak > (int)AMPLITUDE || peak < 0.99 * AMPLITUDE) fail = 1;
    if (err_max > 2.0 * M_PI / (4 * LUT_SIZE))       fail = 1;   // Phasenquantisierung
    return fail;
}

int main() {
    int fail = 0;
    hls::stream<sample_t> s;

    // Reset-Zustand: phase_inc = 0 -> Stille
    for (int n = 0; n < 100; n++) {
        sine_gen(0, s);
        if (s.read() != 0) fail = 1;
    }
    std::printf("Stille bei phase_inc=0: %s\n", fail ? "FEHLER" : "ok");

    fail |= check_tone(100.0);
    fail |= check_tone(440.0);
    fail |= check_tone(1000.0);
    fail |= check_tone(10000.0);

    std::printf(fail ? "TEST FEHLGESCHLAGEN\n" : "TEST OK\n");
    return fail;
}
