#!/usr/bin/env bash
# =============================================================================
#  Messreihe: bis zu welcher Abtastrate empfaengt das ZedBoard vom HackRF lueckenlos?
#
#  Aufruf: ./hackrf_messreihe.sh             je 20 Sekunden bei 2, 4, 8, 10, 12.5, 16, 20 Msps
#          ./hackrf_messreihe.sh 60          je 60 Sekunden
#          ./hackrf_messreihe.sh 20 8 10     nur 8 und 10 Msps
#
#  Startet auf dem Board iq_rx (Daten werden verworfen) und fasst je Abtastrate zusammen:
#  gemessene Rate, mittlere und hoechste CPU-Last (100 % = ein Kern), Luecken laut HackRF.
#  Die vollstaendigen Ausgaben stehen danach in logs/hackrf_messreihe.log.
# =============================================================================
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
SECONDS_EACH="${1:-20}"
shift || true
RATES=("$@")
[[ ${#RATES[@]} -gt 0 ]] || RATES=(2 4 8 10 12.5 16 20)
LOG="$DIR/logs/hackrf_messreihe.log"
mkdir -p "$DIR/logs"

{ echo "== $(date '+%F %T') =="; "$DIR/board.sh" run hackrf_info; } 2>&1 | tee "$LOG" | grep -E "Board ID|Firmware Version|No HackRF" || true
grep -q "Firmware Version" "$LOG" || { echo "FEHLER: hackrf_info findet kein HackRF, siehe $LOG" >&2; exit 1; }

printf '\n%6s  %10s  %9s  %9s  %8s  %s\n' "Msps" "gemessen" "CPU Mitt." "CPU max." "Luecken" "Ergebnis"
for rate in "${RATES[@]}"; do
    out="$("$DIR/board.sh" run iq_rx -s "${rate}e6" -t "$SECONDS_EACH" 2>&1 || true)"
    { echo; echo "== $rate Msps =="; echo "$out"; } >> "$LOG"
    # Zeilen je Sekunde: "... CPU  23 %  Luecken 0"; die erste Sekunde enthaelt den Anlauf
    cpu="$(awk '/ CPU / && / s / { for (i = 1; i <= NF; i++) if ($i == "CPU") { v = $(i + 1); n++; if (n > 1) { s += v; m++; if (v > max) max = v } } }
                END { if (m) printf "%.0f %.0f", s / m, max; else print "? ?" }' <<< "$out")"
    measured="$(sed -n 's/.* \([0-9.]*\) Msps gemessen.*/\1/p' <<< "$out")"
    gaps="$(sed -n 's/^Luecken laut HackRF: \([0-9]*\).*/\1/p' <<< "$out")"
    result="$(sed -n 's/^ERGEBNIS: //p' <<< "$out")"
    [[ -n "$result" ]] || result="$(grep -m 1 FEHLER <<< "$out" || echo "keine Ausgabe")"
    printf '%6s  %10s  %7s %%  %7s %%  %8s  %s\n' "$rate" "${measured:-?}" "${cpu% *}" "${cpu#* }" "${gaps:-?}" "$result"
done
echo
echo "Einzelheiten: $LOG"
