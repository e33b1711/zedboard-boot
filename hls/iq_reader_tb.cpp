// C-Testbench: fuellt einen kleinen Ring im Speichermodell mit Zaehlerdaten und prueft,
// dass der Core sie lueckenlos und in der richtigen Reihenfolge ausgibt – auch ueber das
// Ringende hinweg –, dass er einen Sprung in den Daten zaehlt und dass "enable = 0" alles
// zuruecksetzt.
#include "iq_reader.h"
#include <cstdio>

#define RING_ADDR  (4 * IQ_CHUNK_BYTES)          // Ring liegt nicht am Anfang des Speichers
#define RING_BYTES (8 * IQ_CHUNK_BYTES)

static ap_uint<64> mem[IQ_MEM_WORDS];
static hls::stream<ap_uint<16> > out;
static unsigned wr = 0, next_written = 0, next_expected = 0;
static ap_uint<32> rd, samples, seq_errors;
static int errors = 0;

static void check(bool ok, const char *what) {
    if (!ok) { errors++; printf("FEHLER: %s\n", what); }
}

// Bloecke mit fortlaufenden Zaehlerwerten in den Ring schreiben, wie es Linux tut
static void write_blocks(int blocks) {
    for (int n = 0; n < blocks * IQ_CHUNK_SAMPLES; n++) {
        unsigned byte = RING_ADDR + wr;
        mem[byte / 8](16 * ((byte / 2) % 4) + 15, 16 * ((byte / 2) % 4)) = next_written++ & 0xFFFF;
        wr = (wr + 2) % RING_BYTES;
    }
}

static void run(int enable, int calls) {
    for (int n = 0; n < calls; n++)
        iq_reader(enable, RING_ADDR, RING_BYTES, wr, &rd, &samples, &seq_errors, mem, out);
}

// Alles lesen, was der Core ausgegeben hat; Rueckgabe = Anzahl
static int drain() {
    int n = 0;
    while (!out.empty()) {
        unsigned s = out.read();
        if (s != (next_expected & 0xFFFF)) {
            errors++;
            if (errors <= 10) printf("FEHLER: Abtastwert %u statt %u\n", s, next_expected & 0xFFFF);
        }
        next_expected++;
        n++;
    }
    return n;
}

int main() {
    run(0, 1);
    check(rd == 0 && samples == 0, "Anfangszustand");

    // 1. drei Bloecke, mehr Aufrufe als noetig
    write_blocks(3);
    run(1, 5);
    check(drain() == 3 * IQ_CHUNK_SAMPLES, "drei Bloecke ausgegeben");
    check(rd == wr && samples == 3 * IQ_CHUNK_SAMPLES && seq_errors == 0, "Zeiger und Zaehler nach drei Bloecken");

    // 2. ueber das Ringende hinweg
    write_blocks(6);
    check(wr == 1 * IQ_CHUNK_BYTES, "Schreibzeiger ist umgelaufen");
    run(1, 8);
    check(drain() == 6 * IQ_CHUNK_SAMPLES, "sechs Bloecke ueber das Ringende");
    check(rd == wr && seq_errors == 0, "Zeiger und Zaehler nach dem Umlauf");

    // 3. ein Sprung in den Daten wird gezaehlt, die Daten kommen trotzdem unveraendert an
    next_written += 5;
    next_expected += 5;
    write_blocks(1);
    run(1, 2);
    check(drain() == IQ_CHUNK_SAMPLES && seq_errors == 1, "Sprung wird einmal gezaehlt");

    // 4. ohne neue Daten passiert nichts
    run(1, 3);
    check(out.empty() && samples == 10 * IQ_CHUNK_SAMPLES, "kein Block ohne neue Daten");

    // 5. anhalten: ungelesene Daten werden uebersprungen, Zaehler geloescht
    write_blocks(2);
    run(0, 1);
    check(out.empty() && rd == wr && samples == 0 && seq_errors == 0, "Anhalten setzt zurueck");
    next_expected = next_written;
    write_blocks(1);
    run(1, 2);
    check(drain() == IQ_CHUNK_SAMPLES && seq_errors == 0, "Neustart nach dem Anhalten");

    if (errors) { printf("TEST FEHLGESCHLAGEN: %d Fehler\n", errors); return 1; }
    printf("TEST OK\n");
    return 0;
}
