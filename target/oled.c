/* =============================================================================
 *  Text auf der OLED-Anzeige des ZedBoards ausgeben
 *
 *  Aufruf: oled "Zeile 1" "Zeile 2"   zwei Zeilen zu je 16 Zeichen anzeigen
 *          oled "Zeile 1"             zweite Zeile leer
 *          oled -u "Zeile 1"          Bild um 180 Grad gedreht
 *          oled aus                   Anzeige abschalten
 *
 *  Das Bild bleibt stehen, bis es ueberschrieben oder die Anzeige abgeschaltet wird.
 *  Ansteuerung siehe oled.h. Ein stehendes Bild brennt sich bei OLED mit der Zeit ein:
 *  die Anzeige nicht tagelang mit demselben Text laufen lassen.
 * ============================================================================= */
#include "oled.h"

static int usage(void) {
    fprintf(stderr, "Aufruf: oled [-u] \"Zeile 1\" [\"Zeile 2\"]   Text anzeigen (-u = um 180 Grad gedreht)\n"
                    "        oled aus                         Anzeige abschalten\n");
    return 1;
}

int main(int argc, char **argv) {
    int flip = argc > 1 && !strcmp(argv[1], "-u");
    argv += flip;
    argc -= flip;
    if (argc < 2 || argc > 3 || argv[1][0] == '-') return usage();

    if (oled_open()) return 2;
    if (argc == 2 && !strcmp(argv[1], "aus")) {
        oled_off();
        return 0;
    }
    if (oled_init(flip) || oled_text(argv[1], argc > 2 ? argv[2] : NULL)) return 2;
    return 0;
}
