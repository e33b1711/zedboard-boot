#!/usr/bin/env bash
# =============================================================================
#  SD-Karte fuer ZedBoard vorbereiten (manueller Weg, Alternative zum .wic)
#
#  Aufruf:  sudo ./prepare_sd.sh /dev/sdX /pfad/zu/projekt/images/linux
#
#  Layout:  p1  FAT32  512 MiB  "BOOT"   BOOT.BIN, boot.scr, image.ub
#           p2  ext4   Rest     "rootfs" entpacktes rootfs.tar.gz
#  ACHTUNG: Loescht das komplette Zielgeraet!
# =============================================================================
set -euo pipefail

DEV="${1:?Geraet angeben, z.B. /dev/sdb oder /dev/mmcblk0}"
IMG="$(realpath "${2:?Pfad zu images/linux angeben}")"

[[ $EUID -eq 0 ]] || { echo "Bitte mit sudo ausfuehren." >&2; exit 1; }
[[ -b "$DEV" ]]   || { echo "$DEV ist kein Blockgeraet." >&2; exit 1; }
for f in BOOT.BIN boot.scr image.ub rootfs.tar.gz; do
    [[ -f "$IMG/$f" ]] || { echo "Fehlt: $IMG/$f" >&2; exit 1; }
done

# Schutz: nicht das Systemlaufwerk
ROOTDEV="$(lsblk -no PKNAME "$(findmnt -no SOURCE /)" 2>/dev/null || true)"
if [[ -n "$ROOTDEV" && "$DEV" == "/dev/$ROOTDEV" ]]; then
    echo "$DEV enthaelt das Root-Dateisystem dieses Rechners – Abbruch." >&2; exit 1
fi

lsblk "$DEV"
read -r -p "ALLE Daten auf $DEV loeschen? Tippe 'ja': " ok
[[ "$ok" == "ja" ]] || { echo "Abgebrochen."; exit 1; }

# Partitionsnamen (sdb1 vs. mmcblk0p1)
if [[ "$DEV" =~ [0-9]$ ]]; then P1="${DEV}p1"; P2="${DEV}p2"; else P1="${DEV}1"; P2="${DEV}2"; fi

umount "${DEV}"* 2>/dev/null || true
wipefs -a "$DEV"

sfdisk "$DEV" <<EOF
label: dos
start=4MiB, size=512MiB, type=c, bootable
type=83
EOF
partprobe "$DEV"; sleep 2

mkfs.vfat -F 32 -n BOOT "$P1"
mkfs.ext4 -F -L rootfs "$P2"

MNT="$(mktemp -d)"
mkdir -p "$MNT/boot" "$MNT/root"
mount "$P1" "$MNT/boot"
mount "$P2" "$MNT/root"

cp "$IMG"/BOOT.BIN "$IMG"/boot.scr "$IMG"/image.ub "$MNT/boot/"
tar -xzpf "$IMG/rootfs.tar.gz" -C "$MNT/root"

sync
umount "$MNT/boot" "$MNT/root"
rmdir "$MNT/boot" "$MNT/root" "$MNT"
echo "SD-Karte fertig."
