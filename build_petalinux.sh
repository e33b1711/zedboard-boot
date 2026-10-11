#!/usr/bin/env bash
# =============================================================================
#  ZedBoard – PetaLinux-Projekt aus XSA erzeugen und bauen (2024.x / 2025.x)
#
#  Voraussetzung: source <petalinux-install>/settings.sh
#  Aufruf:        ./build_petalinux.sh /pfad/zu/system.xsa [projektname]
#
#  Ergebnis in <projekt>/images/linux/:
#     BOOT.BIN   (FSBL + Bitstream + U-Boot)
#     boot.scr   (U-Boot Distro-Boot-Skript)
#     image.ub   (FIT: Kernel + Device Tree)
#     rootfs.tar.gz / rootfs.ext4
#     petalinux-sdimage.wic  (fertiges SD-Image, optional)
# =============================================================================
set -euo pipefail

XSA="$(realpath "${1:?Pfad zur system.xsa angeben}")"
PROJ="${2:-zedboard-plnx}"

command -v petalinux-create >/dev/null || {
    echo "FEHLER: PetaLinux-Umgebung nicht geladen (source settings.sh)." >&2; exit 1; }
[[ -f "$XSA" ]] || { echo "FEHLER: $XSA existiert nicht." >&2; exit 1; }

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

# --- Projekt anlegen (neue Syntax ab 2024.1; Fallback auf alte Syntax) -------
if [[ ! -d "$PROJ" ]]; then
    petalinux-create project --template zynq --name "$PROJ" \
      || petalinux-create -t project --template zynq --name "$PROJ"
fi
cd "$PROJ"

# --- Hardware importieren ---------------------------------------------------
petalinux-config --get-hw-description="$XSA" --silentconfig

# --- Kconfig-Werte setzen ---------------------------------------------------
CFG=project-spec/configs/config
ROOTFS_CFG=project-spec/configs/rootfs_config
setcfg() {   # setcfg KEY VALUE [DATEI]   (VALUE "n" => deaktivieren)
    local key="$1" val="$2" file="${3:-$CFG}"
    sed -i -E "/^(# )?${key}[= ].*/d" "$file"
    if [[ "$val" == "n" ]]; then echo "# ${key} is not set" >> "$file"
    else echo "${key}=${val}" >> "$file"; fi
}

# Board-Name -> ZedBoard-spezifische DT-Teile (PHY, LEDs usw.)
setcfg CONFIG_SUBSYSTEM_MACHINE_NAME '"zedboard"'
# Root-Filesystem als ext4 auf SD-Partition 2 statt initramfs
setcfg CONFIG_SUBSYSTEM_ROOTFS_INITRD n
setcfg CONFIG_SUBSYSTEM_ROOTFS_INITRAMFS n
setcfg CONFIG_SUBSYSTEM_ROOTFS_EXT4 y
setcfg CONFIG_SUBSYSTEM_SDROOT_DEV '"/dev/mmcblk0p2"'
# Konsole auf UART1 (USB-UART J14)
setcfg CONFIG_SUBSYSTEM_SERIAL_PS7_UART_1_SELECT y

# --- Device-Tree-Ergaenzung -------------------------------------------------
DTSI_DST=project-spec/meta-user/recipes-bsp/device-tree/files/system-user.dtsi
if [[ -f "$SCRIPT_DIR/system-user.dtsi" ]]; then
    cp "$SCRIPT_DIR/system-user.dtsi" "$DTSI_DST"
fi

# --- Werkzeuge fuers Board (Sinuston, HDMI-Testbild, Framebuffer, HackRF) und i2c-tools ins Rootfs --
APP_DIR=project-spec/meta-user/recipes-apps/zed-tools
USER_ROOTFS=project-spec/meta-user/conf/user-rootfsconfig
# Vorgaenger-Rezept aus aelteren Projektstaenden entfernen
rm -rf project-spec/meta-user/recipes-apps/zed-tone
sed -i '/^CONFIG_zed-tone$/d' "$USER_ROOTFS"
sed -i -E '/^(# )?CONFIG_zed-tone[= ]/d' "$ROOTFS_CFG"

mkdir -p "$APP_DIR/files"
cp "$SCRIPT_DIR/target/zed-tools.bb" "$APP_DIR/"
cp "$SCRIPT_DIR"/target/*.sh "$SCRIPT_DIR"/target/*.c "$SCRIPT_DIR"/target/*.h "$SCRIPT_DIR"/target/uio.conf "$APP_DIR/files/"
# libhackrf und die HackRF-Werkzeuge; kommen als Abhaengigkeit von zed-tools ins Rootfs
mkdir -p project-spec/meta-user/recipes-apps/hackrf
cp "$SCRIPT_DIR/target/hackrf.bb" project-spec/meta-user/recipes-apps/hackrf/

# --- SSH fuer board.sh: fester Host-Schluessel und Schluessel des PCs fuer root --
# Der Host-Schluessel liegt in ./keys (nicht im Git) und bleibt ueber alle Builds gleich,
# damit board.sh das Board daran wiedererkennt. ZED_SSH_PUBKEY waehlt den PC-Schluessel.
KEY_DIR="$SCRIPT_DIR/keys"
mkdir -p "$KEY_DIR"
[[ -f "$KEY_DIR/ssh_host_ed25519_key" ]] \
    || ssh-keygen -q -t ed25519 -N "" -C "zedboard" -f "$KEY_DIR/ssh_host_ed25519_key"
cp "$KEY_DIR/ssh_host_ed25519_key" "$KEY_DIR/ssh_host_ed25519_key.pub" "$APP_DIR/files/"
PUBKEY="${ZED_SSH_PUBKEY:-$HOME/.ssh/id_ed25519.pub}"
if [[ -f "$PUBKEY" ]]; then
    cp "$PUBKEY" "$APP_DIR/files/authorized_keys"
else
    echo "WARN: $PUBKEY fehlt – kein SSH-Zugang ohne Passwort (ssh-keygen -t ed25519, dann neu bauen)."
    : > "$APP_DIR/files/authorized_keys"
fi

grep -qx 'CONFIG_zed-tools' "$USER_ROOTFS" || echo 'CONFIG_zed-tools' >> "$USER_ROOTFS"
setcfg CONFIG_zed-tools y "$ROOTFS_CFG"
setcfg CONFIG_i2c-tools y "$ROOTFS_CFG"

petalinux-config --silentconfig

# --- Bauen ------------------------------------------------------------------
petalinux-build

# --- BOOT.BIN packen --------------------------------------------------------
IMG=images/linux
# Bitstream direkt aus der XSA nehmen: petalinux-build erneuert images/linux/system.bit nicht
# in jedem Fall (beobachtet nach einem einzelnen "petalinux-build -c device-tree"), dann
# passen Bitstream und Device Tree in BOOT.BIN nicht zusammen.
unzip -p "$XSA" "$(unzip -Z1 "$XSA" '*.bit' | head -n 1)" > "$IMG/system.bit.neu"
mv "$IMG/system.bit.neu" "$IMG/system.bit"
petalinux-package boot --fsbl "$IMG/zynq_fsbl.elf" --fpga "$IMG/system.bit" --u-boot --force \
  || petalinux-package --boot --fsbl "$IMG/zynq_fsbl.elf" --fpga "$IMG/system.bit" --u-boot --force

# --- Optional: fertiges SD-Karten-Image -------------------------------------
petalinux-package wic --images-dir "$IMG" \
  || petalinux-package --wic --images-dir "$IMG" \
  || echo "WARN: wic-Image nicht erzeugt – SD-Karte mit prepare_sd.sh beschreiben."

echo
echo "FERTIG. Boot-Dateien in $(pwd)/$IMG:"
ls -lh "$IMG"/{BOOT.BIN,boot.scr,image.ub,rootfs.tar.gz} 2>/dev/null || true
