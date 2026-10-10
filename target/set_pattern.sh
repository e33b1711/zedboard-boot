#!/bin/sh
# =============================================================================
#  Testbild des HDMI-Generators (HLS-Core pattern_gen) waehlen oder anzeigen
#
#  Aufruf: set_pattern.sh 1            Farbbalken
#          set_pattern.sh 4 ff8000     Vollflaeche in der Farbe RRGGBB
#          set_pattern.sh 7 60         Framebuffer 0, 1, 2 im Wechsel, jeder 60 Bilder lang
#          set_pattern.sh              aktuelle Einstellung anzeigen
#          set_pattern.sh format 1     Fehlersuche: Bit 0 = Cb/Cr tauschen, Bit 1 = Y/C tauschen
#
#  Testbilder: 0 schwarz, 1 Farbbalken, 2 Graukeil, 3 Schachbrett,
#              4 Vollflaeche, 5 bewegtes Quadrat, 6 wandernder Farbverlauf,
#              7 Framebuffer (vorher mit fb_test fuellen)
#  Register:   pattern 0x10, color 0x18, format 0x20, fb_frames 0x40
# =============================================================================
set -e

NAMES="schwarz Farbbalken Graukeil Schachbrett Vollflaeche Quadrat Farbverlauf Framebuffer"

# Basisadresse aus dem Device Tree (Knoten pattern_gen@<adresse>), sonst Vorgabe
NODE=$(find /proc/device-tree/ -maxdepth 2 -name 'pattern_gen@*' 2>/dev/null | head -n 1)
if [ -n "$NODE" ]; then BASE=0x${NODE##*@}; else BASE=0x40010000; fi
reg() { printf '0x%08X' $((BASE + $1)); }

usage() { sed -n '3,15p' "$0" | sed 's/^# \{0,2\}//' >&2; exit 1; }

if [ $# -eq 0 ]; then
    P=$(($(devmem "$(reg 0x10)" 32) & 0xFF))
    C=$(($(devmem "$(reg 0x18)" 32) & 0xFFFFFF))
    F=$(($(devmem "$(reg 0x20)" 32) & 0x3))
    N=$(($(devmem "$(reg 0x40)" 32) & 0xFFFF))
    printf 'Testbild %d (%s), Farbe %06x, Format %d, Framebuffer-Wechsel alle %d Bilder\n' \
        "$P" "$(echo $NAMES | cut -d' ' -f$((P + 1)))" "$C" "$F" "$N"
    exit 0
fi

if [ "$1" = "format" ]; then
    case "$2" in [0-3]) ;; *) usage ;; esac
    devmem "$(reg 0x20)" 32 "$2"
    echo "Format $2"
    exit 0
fi

case "$1" in [0-7]) ;; *) usage ;; esac
if [ "$1" = 7 ]; then
    if [ -n "$2" ]; then
        echo "$2" | grep -Eq '^[0-9]{1,5}$' && [ "$2" -le 65535 ] || usage
        devmem "$(reg 0x40)" 32 "$2"
    fi
    devmem "$(reg 0x10)" 32 7
    echo "Testbild 7 (Framebuffer)${2:+, Wechsel alle $2 Bilder}"
    exit 0
fi
if [ -n "$2" ]; then
    echo "$2" | grep -Eq '^[0-9a-fA-F]{6}$' || usage
    devmem "$(reg 0x18)" 32 "0x$2"
fi
devmem "$(reg 0x10)" 32 "$1"
echo "Testbild $1 ($(echo $NAMES | cut -d' ' -f$(($1 + 1))))${2:+, Farbe $2}"
