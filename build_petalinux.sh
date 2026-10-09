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

# --- Sinuston: Skripte (codec_init.sh, set_freq.sh) und i2c-tools ins Rootfs --
APP_DIR=project-spec/meta-user/recipes-apps/zed-tone
mkdir -p "$APP_DIR/files"
cp "$SCRIPT_DIR/target/zed-tone.bb" "$APP_DIR/"
cp "$SCRIPT_DIR/target/codec_init.sh" "$SCRIPT_DIR/target/set_freq.sh" "$APP_DIR/files/"
USER_ROOTFS=project-spec/meta-user/conf/user-rootfsconfig
grep -qx 'CONFIG_zed-tone' "$USER_ROOTFS" || echo 'CONFIG_zed-tone' >> "$USER_ROOTFS"
setcfg CONFIG_zed-tone y "$ROOTFS_CFG"
setcfg CONFIG_i2c-tools y "$ROOTFS_CFG"

petalinux-config --silentconfig

# --- Bauen ------------------------------------------------------------------
petalinux-build

# --- BOOT.BIN packen --------------------------------------------------------
IMG=images/linux
petalinux-package boot --fsbl "$IMG/zynq_fsbl.elf" --fpga "$IMG/system.bit" --u-boot --force \
  || petalinux-package --boot --fsbl "$IMG/zynq_fsbl.elf" --fpga "$IMG/system.bit" --u-boot --force

# --- Optional: fertiges SD-Karten-Image -------------------------------------
petalinux-package wic --images-dir "$IMG" \
  || petalinux-package --wic --images-dir "$IMG" \
  || echo "WARN: wic-Image nicht erzeugt – SD-Karte mit prepare_sd.sh beschreiben."

echo
echo "FERTIG. Boot-Dateien in $(pwd)/$IMG:"
ls -lh "$IMG"/{BOOT.BIN,boot.scr,image.ub,rootfs.tar.gz} 2>/dev/null || true
