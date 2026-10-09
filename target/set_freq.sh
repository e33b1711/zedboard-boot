#!/bin/sh
# =============================================================================
#  Frequenz des Sinus-Generators (HLS-Core sine_gen) setzen oder lesen
#
#  Aufruf: set_freq.sh 440       Ton mit 440 Hz
#          set_freq.sh 0         Stille
#          set_freq.sh           aktuelle Frequenz anzeigen
#
#  Register phase_inc (Offset 0x10):  f = phase_inc * 48000 / 2^32
# =============================================================================
set -e

FS=48000
REG_OFFSET=0x10

# Basisadresse aus dem Device Tree (Knoten sine_gen@<adresse>), sonst Vorgabe
NODE=$(find /proc/device-tree -maxdepth 2 -name 'sine_gen@*' 2>/dev/null | head -n 1)
if [ -n "$NODE" ]; then BASE=0x${NODE##*@}; else BASE=0x40000000; fi
REG=$(printf '0x%08X' $((BASE + REG_OFFSET)))

if [ $# -eq 0 ]; then
    INC=$(devmem "$REG" 32)
    awk -v inc="$((INC))" -v fs=$FS 'BEGIN { printf "%.3f Hz (phase_inc = %d)\n", inc * fs / 4294967296, inc }'
    exit 0
fi

INC=$(awk -v f="$1" -v fs=$FS 'BEGIN {
    if (f !~ /^[0-9]+(\.[0-9]+)?$/ || f >= fs / 2) exit 1
    printf "%.0f", f * 4294967296 / fs
}') || { echo "Frequenz muss eine Zahl von 0 bis unter $((FS / 2)) Hz sein" >&2; exit 1; }

devmem "$REG" 32 "$INC"
echo "$1 Hz -> phase_inc = $INC (Register $REG)"
