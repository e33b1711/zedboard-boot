#!/bin/sh
# =============================================================================
#  I/Q-Oszilloskop (rtl/iq_scope.v) im HDMI-Bild einstellen oder anzeigen
#
#  Aufruf: set_scope.sh kreis               eingebautes Testsignal: Kreis
#          set_scope.sh lissajous           eingebautes Testsignal: Lissajous-Figur
#          set_scope.sh iq                  Abtastwerte vom I/Q-Eingang
#          set_scope.sh spektrum            Spektrum der I/Q-Daten (Frequenz nach rechts, Pegel
#                                           nach oben: ganze Hoehe = 96 dB, oben = Vollaussteuerung)
#          set_scope.sh aus                 Einblendung aus
#          set_scope.sh                     aktuelle Einstellung anzeigen
#          set_scope.sh farbe=glut nachleuchten=5     einzelne Werte aendern
#
#  Werte:  farbe=gruen|glut|eis|grau
#          nachleuchten=0..15    Abklingen ueber etwa 2^n Bilder (0 = kein Nachleuchten)
#          schritt=1..255        Zuwachs einer Zelle pro Treffer
#          helligkeit=0..15      0 = am hellsten; jede Stufe halbiert die Anzeige
#          hintergrund=schwarz|bild      "bild" laesst das Testbild durchscheinen
#          gitter=0|1            Achsenkreuz und Rahmen
#          mittelung=0..10       Spektrum: Leistung ueber 2^n FFT-Bloecke mitteln
#          maxhold=0|1|neu       Spektrum: Max-Hold-Kurve aus, ein oder neu beginnen
#  Mehrere Angaben in einem Aufruf sind moeglich: set_scope.sh iq farbe=eis schritt=4
#  Register: AXI-GPIO mit 32 Ausgaengen, Belegung siehe rtl/iq_scope.v und rtl/spec_trace.v;
#            Core iq_spectrum: avg_log2 0x10, maxhold 0x18
# =============================================================================
set -e

COLORS="gruen glut eis grau"
DEFAULT=0x00440131      # Gitter, Helligkeit 4, Schritt 1, Nachleuchten 3, ein

usage() { sed -n '3,23p' "$0" | sed 's/^# \{0,2\}//' >&2; exit 1; }

# Basisadresse: das AXI-GPIO mit 32 Bit Breite (das der LEDs hat 8)
BASE=
for NODE in $(find /proc/device-tree/ -maxdepth 2 -name 'gpio@4*' 2>/dev/null); do
    WIDTH=$(hexdump -v -e '1/1 "%02x"' "$NODE/xlnx,gpio-width" 2>/dev/null)
    if [ "$WIDTH" = 00000020 ]; then BASE=0x${NODE##*@}; fi
done
[ -n "$BASE" ] || { echo "FEHLER: AXI-GPIO des Oszilloskops nicht im Device Tree (Bitstream ohne iq_scope?)" >&2; exit 1; }

V=$(($(devmem "$BASE" 32)))

# Core des Spektrums (fehlt in aelteren Bitstreams)
SPEC=$(find /proc/device-tree/ -maxdepth 2 -name 'iq_spectrum@*' 2>/dev/null | head -n 1)
[ -z "$SPEC" ] || SPEC=0x${SPEC##*@}
spec_reg() { printf '0x%08X' $((SPEC + $1)); }
need_spec() { [ -n "$SPEC" ] || { echo "FEHLER: Core iq_spectrum nicht im Device Tree (Bitstream ohne Spektrum?)" >&2; exit 1; }; }

field() { echo $(((V >> $1) & $2)); }               # field BIT MASKE
put()   { V=$(((V & ~($3 << $2)) | (($1 & $3) << $2))); }   # put WERT BIT MASKE
number() {                                           # number TEXT MAX
    echo "$1" | grep -Eq '^[0-9]{1,3}$' && [ "$1" -le "$2" ] || usage
}

show() {
    if [ "$(field 0 1)" = 0 ]; then STATE=aus
    elif [ "$(field 23 1)" = 1 ]; then STATE=Spektrum
    elif [ "$(field 1 1)" = 0 ]; then STATE="I/Q-Eingang"
    elif [ "$(field 2 1)" = 0 ]; then STATE="Testsignal Kreis"
    else STATE="Testsignal Lissajous"; fi
    if [ "$(field 3 1)" = 1 ]; then BG=bild; else BG=schwarz; fi
    printf 'Oszilloskop %s: farbe=%s nachleuchten=%d schritt=%d helligkeit=%d hintergrund=%s gitter=%d\n' \
        "$STATE" "$(echo $COLORS | cut -d' ' -f$(($(field 20 3) + 1)))" \
        "$(field 4 15)" "$(field 8 255)" "$(field 16 15)" "$BG" "$(field 22 1)"
    if [ -n "$SPEC" ] && [ "$(field 23 1)" = 1 ]; then
        printf 'Spektrum: mittelung=%d (%d Bloecke) maxhold=%d, %d Spektren ausgegeben, %d FFT-Ueberlaeufe\n' \
            "$(($(devmem "$(spec_reg 0x10)" 32) & 15))" "$((1 << ($(devmem "$(spec_reg 0x10)" 32) & 15)))" \
            "$(field 24 1)" "$(($(devmem "$(spec_reg 0x20)" 32)))" "$(($(devmem "$(spec_reg 0x30)" 32)))"
    fi
}

if [ $# -eq 0 ]; then show; exit 0; fi

# Erstes Einschalten nach dem Start: sinnvolle Vorgabe statt lauter Nullen
[ "$V" != 0 ] || [ "$1" = aus ] || V=$((DEFAULT & ~1))

for ARG in "$@"; do
    case "$ARG" in
    aus)        put 0 0 1 ;;
    kreis)      put 1 0 1; put 1 1 1; put 0 2 1; put 0 23 1 ;;
    lissajous)  put 1 0 1; put 1 1 1; put 1 2 1; put 0 23 1 ;;
    iq)         put 1 0 1; put 0 1 1; put 0 23 1 ;;
    spektrum)   need_spec
                # jede Zelle der Kurve wird etwa 71 mal pro Sekunde einmal getroffen:
                # dazu passen kurzes Nachleuchten und ein grosser Schritt
                put 1 0 1; put 0 1 1; put 1 23 1; put 2 4 15; put 32 8 255; put 0 16 15 ;;
    mittelung=*) need_spec; number "${ARG#*=}" 10; devmem "$(spec_reg 0x10)" 32 "${ARG#*=}" ;;
    maxhold=0)  need_spec; put 0 24 1; devmem "$(spec_reg 0x18)" 32 0 ;;
    maxhold=1)  need_spec; put 1 24 1; devmem "$(spec_reg 0x18)" 32 1 ;;
    maxhold=neu) need_spec; put 1 24 1; devmem "$(spec_reg 0x18)" 32 0; sleep 1; devmem "$(spec_reg 0x18)" 32 1 ;;
    farbe=*)
        N=0; FOUND=
        for C in $COLORS; do
            if [ "$C" = "${ARG#*=}" ]; then FOUND=$N; fi
            N=$((N + 1))
        done
        [ -n "$FOUND" ] || usage
        put "$FOUND" 20 3 ;;
    nachleuchten=*) number "${ARG#*=}" 15;  put "${ARG#*=}" 4 15 ;;
    schritt=*)      number "${ARG#*=}" 255; [ "${ARG#*=}" -ge 1 ] || usage; put "${ARG#*=}" 8 255 ;;
    helligkeit=*)   number "${ARG#*=}" 15;  put "${ARG#*=}" 16 15 ;;
    hintergrund=schwarz) put 0 3 1 ;;
    hintergrund=bild)    put 1 3 1 ;;
    gitter=0)   put 0 22 1 ;;
    gitter=1)   put 1 22 1 ;;
    *)          usage ;;
    esac
done

devmem "$BASE" 32 "$(printf '0x%08X' "$V")"
show
