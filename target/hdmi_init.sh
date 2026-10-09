#!/bin/sh
# =============================================================================
#  ADV7511 (HDMI-Sender, ZedBoard) fuer das Testbild einrichten
#
#  Aufruf: hdmi_init.sh            (als root, nach jedem Boot einmal)
#
#  Eingang: 16 Bit YCbCr 4:2:2, getrennte Syncs, 1280x720 bei 60 Hz
#  Ausgang: HDMI, RGB 4:4:4 (Farbwandlung im ADV7511)
# =============================================================================
set -e

ADDR=0x39

# I2C-Bus des PS-I2C1 (Cadence-Controller auf e0005000) suchen
BUS=$(i2cdetect -l | awk '/e0005000/ { sub("i2c-", "", $1); print $1; exit }')
[ -n "$BUS" ] || { echo "PS-I2C1 (e0005000) nicht gefunden" >&2; exit 1; }

wr() { i2cset -y "$BUS" $ADDR "$1" "$2"; }
rd() { i2cget -y "$BUS" $ADDR "$1"; }

ID="$(rd 0xF5)$(rd 0xF6)"
[ "$ID" = "0x750x11" ] || { echo "Kein ADV7511 auf i2c-$BUS, Adresse $ADDR (Chip-ID: $ID)" >&2; exit 1; }

wr 0xD6 0xC0    # Hot Plug Detect intern immer aktiv -> Chip bleibt auch ohne Monitor an
wr 0x41 0x10    # Power-up

# Feste Werte, die das Datenblatt nach dem Einschalten verlangt
wr 0x98 0x03
wr 0x9A 0xE0
wr 0x9C 0x30
wr 0x9D 0x61
wr 0xA2 0xA4
wr 0xA3 0xA4
wr 0xE0 0xD0
wr 0xF9 0x00

# Videoeingang
wr 0x15 0x01    # Input ID 1: 16 Bit YCbCr 4:2:2, getrennte Syncs
wr 0x16 0x38    # Ausgang 4:4:4, 8 Bit, Input Style 1, Ausgang RGB
wr 0x17 0x06    # 16:9, 4:2:2 -> 4:4:4 mit Interpolation, Sync-Polaritaet unveraendert
wr 0x48 0x08    # Daten rechtsbuendig
wr 0xBA 0x60    # keine Taktverzoegerung

# Farbwandlung YCbCr (BT.709, begrenzter Bereich) -> RGB (voller Bereich)
wr 0x18 0xC7; wr 0x19 0x34; wr 0x1A 0x04; wr 0x1B 0xAD
wr 0x1C 0x00; wr 0x1D 0x00; wr 0x1E 0x1C; wr 0x1F 0x1B
wr 0x20 0x1D; wr 0x21 0xDC; wr 0x22 0x04; wr 0x23 0xAD
wr 0x24 0x1F; wr 0x25 0x24; wr 0x26 0x01; wr 0x27 0x35
wr 0x28 0x00; wr 0x29 0x00; wr 0x2A 0x04; wr 0x2B 0xAD
wr 0x2C 0x08; wr 0x2D 0x7C; wr 0x2E 0x1B; wr 0x2F 0x77

# Ausgang
wr 0xAF 0x06    # HDMI-Modus (0x04 = DVI)
wr 0x55 0x00    # AVI InfoFrame: RGB
wr 0x56 0x28    # AVI InfoFrame: 16:9
wr 0x57 0x08    # AVI InfoFrame: RGB voller Wertebereich

STATE=$(rd 0x42)
if [ $((STATE & 0x40)) -ne 0 ]; then MON="Monitor erkannt"; else MON="kein Monitor erkannt"; fi
echo "ADV7511 initialisiert (i2c-$BUS, Adresse $ADDR), $MON (Status $STATE)."
