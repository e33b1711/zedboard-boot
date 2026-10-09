#!/bin/sh
# =============================================================================
#  ADAU1761 (ZedBoard) fuer Wiedergabe ueber I2S initialisieren
#
#  Aufruf: codec_init.sh            (als root)
#
#  Codec ist I2S-Slave, MCLK = 256 * fs direkt (ohne PLL), fs = 48 kHz.
#  Ausgabe auf Line Out (0 dB) und Kopfhoerer (-20 dB).
# =============================================================================
set -e

ADDR=0x38       # AC-ADR0 = AC-ADR1 = 0

# I2C-Bus des PS-I2C0 (Cadence-Controller auf e0004000) suchen
BUS=$(i2cdetect -l | awk '/e0004000/ { sub("i2c-", "", $1); print $1; exit }')
[ -n "$BUS" ] || { echo "PS-I2C0 (e0004000) nicht gefunden" >&2; exit 1; }

wr() {  # wr REG_HI REG_LO WERT
    i2ctransfer -y "$BUS" w3@$ADDR "$1" "$2" "$3"
}
rd() {  # rd REG_HI REG_LO
    i2ctransfer -y "$BUS" w2@$ADDR "$1" "$2" r1
}

wr 0x40 0x00 0x01   # R0  Clock Control: Core-Takt an, MCLK direkt, 256 * fs
[ "$(rd 0x40 0x00)" = "0x01" ] || { echo "Codec antwortet nicht wie erwartet (R0)" >&2; exit 1; }

wr 0x40 0xF9 0x7F   # R65 Clock Enable 0: alle Module takten
wr 0x40 0xFA 0x03   # R66 Clock Enable 1
wr 0x40 0x15 0x00   # R15 Serial Port 0: Slave, I2S
wr 0x40 0x16 0x00   # R16 Serial Port 1: 64 BCLK pro Frame, 1 Bit Verzoegerung
wr 0x40 0x17 0x00   # R17 Converter 0: fs = 48 kHz
wr 0x40 0xF2 0x01   # R58 Serial Input Route: I2S-Eingang -> DACs
wr 0x40 0x2A 0x03   # R36 DAC Control 0: beide DACs an
wr 0x40 0x1C 0x21   # R22 Mixer 3 (links):  linker DAC, Mixer an
wr 0x40 0x1E 0x41   # R24 Mixer 4 (rechts): rechter DAC, Mixer an
wr 0x40 0x20 0x05   # R26 Mixer 5 (Line Out links):  Mixer 3, an
wr 0x40 0x21 0x11   # R27 Mixer 6 (Line Out rechts): Mixer 4, an
wr 0x40 0x25 0xE6   # R31 Line Out links:  0 dB, nicht stumm
wr 0x40 0x26 0xE6   # R32 Line Out rechts: 0 dB, nicht stumm
wr 0x40 0x23 0x97   # R29 Kopfhoerer links:  -20 dB, nicht stumm, an
wr 0x40 0x24 0x97   # R30 Kopfhoerer rechts: -20 dB, nicht stumm, Kopfhoerer-Modus
wr 0x40 0x29 0x03   # R35 Playback Power: beide Kanaele an

echo "ADAU1761 initialisiert (i2c-$BUS, Adresse $ADDR)."
