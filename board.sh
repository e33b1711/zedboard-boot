#!/usr/bin/env bash
# =============================================================================
#  ZedBoard ueber das Netzwerk aktualisieren und bedienen (SSH als root)
#
#  Aufruf: ./board.sh deploy        Boot-Dateien und Werkzeuge uebertragen, bei
#                                   geaenderten Boot-Dateien neu starten
#          ./board.sh tools         nur die Werkzeuge mit ihren Bibliotheken (ohne Neustart)
#          ./board.sh run BEFEHL    Befehl auf dem Board ausfuehren
#          ./board.sh find          Adresse des Boards ausgeben
#
#  Voraussetzung: Image aus build_petalinux.sh (enthaelt den SSH-Schluessel des PCs
#  und einen festen Host-Schluessel aus ./keys). Das Board wird an seinem
#  Host-Schluessel erkannt, auch wenn es eine neue IP-Adresse bekommen hat.
#  Feste Adresse vorgeben: ZED_HOST=192.168.178.60 ./board.sh ...
#
#  Nicht uebertragen werden Kernel-Module und der Rest des Rootfs: nach Aenderungen
#  an Kernel-Konfiguration oder Paketen die Karte neu schreiben.
# =============================================================================
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
KEYS="$DIR/keys"
IMG="$DIR/${ZED_PROJ:-zedboard-plnx}/images/linux"
BOOT_DEV=/dev/mmcblk0p1

usage() { sed -n '3,10p' "$0" | sed 's/^# \{0,2\}//' >&2; exit 1; }

[[ -f "$KEYS/ssh_host_ed25519_key.pub" ]] || {
    echo "FEHLER: $KEYS/ssh_host_ed25519_key.pub fehlt – zuerst build_petalinux.sh ausfuehren." >&2; exit 1; }
HOSTKEY="$(cut -d' ' -f1,2 "$KEYS/ssh_host_ed25519_key.pub")"
echo "zedboard $HOSTKEY" > "$KEYS/known_hosts"
SSH_OPTS=(-o UserKnownHostsFile="$KEYS/known_hosts" -o StrictHostKeyChecking=yes -o HostKeyAlias=zedboard
          -o BatchMode=yes -o ConnectTimeout=5 -o LogLevel=ERROR)

# Antwortet unter dieser Adresse ein SSH-Server mit dem Host-Schluessel des Boards?
is_board() { ssh-keyscan -T 2 -t ed25519 "$1" 2>/dev/null | grep -qF "$HOSTKEY"; }

# Reihenfolge: ZED_HOST, zuletzt benutzte Adresse, Hostname, Suche im eigenen /24-Netz
find_board() {
    local ip
    if [[ -n "${ZED_HOST:-}" ]]; then echo "$ZED_HOST"; return; fi
    for ip in "$(cat "$KEYS/last_ip" 2>/dev/null || true)" zedboard-plnx; do
        if [[ -n "$ip" ]] && is_board "$ip"; then echo "$ip"; return; fi
    done
    local net
    net="$(ip -4 -o addr show scope global | awk '$4 ~ /\/24$/ { sub(/\.[0-9]+\/24$/, "", $4); print $4; exit }')"
    [[ -n "$net" ]] || { echo "FEHLER: kein /24-Netz gefunden – Adresse mit ZED_HOST vorgeben." >&2; return 1; }
    ip="$(ssh-keyscan -T 3 -t ed25519 $(seq -f "$net.%g" 1 254) 2>/dev/null | grep -F "$HOSTKEY" | head -n 1 | cut -d' ' -f1)"
    [[ -n "$ip" ]] || { echo "FEHLER: Board im Netz $net.0/24 nicht gefunden (eingeschaltet? Netzwerkkabel?)." >&2; return 1; }
    echo "$ip"
}

on_board() { ssh "${SSH_OPTS[@]}" "root@$HOST" "$@"; }

# Werkzeuge aus dem gebauten Rootfs nach /usr/bin, dazu die HackRF-Werkzeuge mit ihren
# Bibliotheken nach /usr/lib und /lib (fehlen in Images von vor dem HackRF-Stand)
deploy_tools() {
    local names=(fb_test iq_rx 'hackrf_*') f
    for f in "$DIR"/target/*.sh; do names+=("$(basename "$f")"); done
    local libs=('usr/lib/libhackrf.so*' 'usr/lib/libfftw3f.so*' 'lib/libusb-1.0.so*' 'lib/libudev.so*')
    local tmp; tmp="$(mktemp -d)"
    tar -xzf "$IMG/rootfs.tar.gz" -C "$tmp" --wildcards "${names[@]/#/./usr/bin/}" "${libs[@]/#/./}"
    tar -C "$tmp" --owner=0 --group=0 -cf - ./usr ./lib | on_board 'tar -C / -xf -'
    echo "Werkzeuge uebertragen: $(ls "$tmp/usr/bin" | tr '\n' ' ')"
    echo "Bibliotheken uebertragen: $(ls "$tmp/usr/lib" "$tmp/lib" | grep '\.so' | tr '\n' ' ')"
    rm -rf "$tmp"
}

# Boot-Dateien auf die FAT-Partition; uebertraegt nur, was sich geaendert hat.
# Rueckgabe 0, wenn etwas geaendert wurde.
deploy_boot() {
    local mnt f changed=1
    mnt="$(on_board "awk '\$1 == \"$BOOT_DEV\" { print \$2; exit }' /proc/mounts")"
    if [[ -z "$mnt" ]]; then
        mnt=/run/zedboot
        on_board "mkdir -p $mnt && mount $BOOT_DEV $mnt"
    fi
    on_board "mount -o remount,rw $mnt"
    for f in $(on_board "ls $mnt"); do
        [[ -f "$IMG/$f" ]] || continue
        local want; want="$(md5sum < "$IMG/$f" | cut -d' ' -f1)"
        [[ "$(on_board "md5sum < $mnt/$f" | cut -d' ' -f1)" == "$want" ]] && continue
        echo "Uebertrage $f ..."
        on_board "cat > $mnt/$f.neu" < "$IMG/$f"
        on_board "sync"
        [[ "$(on_board "md5sum < $mnt/$f.neu" | cut -d' ' -f1)" == "$want" ]] || {
            on_board "rm -f $mnt/$f.neu"
            echo "FEHLER: $f ist auf der Karte nicht identisch angekommen – alte Datei bleibt." >&2; exit 1; }
        on_board "mv $mnt/$f.neu $mnt/$f && sync"
        changed=0
    done
    return $changed
}

reboot_board() {
    echo "Neustart ..."
    on_board "sync; reboot" || true
    sleep 15
    local i
    for i in $(seq 1 30); do                    # die Adresse kann sich geaendert haben
        local cand
        if cand="$(find_board 2>/dev/null)" && is_board "$cand"; then
            HOST="$cand"
            echo "$HOST" > "$KEYS/last_ip"
            echo "Board ist wieder da: $HOST, $(on_board 'uname -r; uptime' | tr '\n' ' ')"
            return
        fi
        sleep 3
    done
    echo "FEHLER: Board meldet sich nach dem Neustart nicht – serielle Konsole pruefen." >&2; exit 1
}

[[ $# -ge 1 ]] || usage
HOST="$(find_board)"
echo "$HOST" > "$KEYS/last_ip"

case "$1" in
find)   echo "$HOST" ;;
run)    shift; [[ $# -ge 1 ]] || usage; on_board "$@" ;;
tools)  deploy_tools ;;
deploy) deploy_tools
        if deploy_boot; then reboot_board; else echo "Boot-Dateien unveraendert, kein Neustart."; fi ;;
*)      usage ;;
esac
