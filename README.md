# ZedBoard – Boot-Konfiguration from scratch (Vivado + PetaLinux, SD-Boot)

## Boot-Kette

```
BootROM ──► FSBL ──► Bitstream (PL) ──► U-Boot ──► boot.scr ──► image.ub (Kernel + DTB) ──► rootfs (ext4, mmcblk0p2)
           └────────── BOOT.BIN ──────────────┘    └──────── FAT-Partition 1 ────────┘
```

## Dateien

| Datei               | Zweck                                                        |
|---------------------|--------------------------------------------------------------|
| `create_hw.tcl`     | Vivado-Projekt: PS7 mit ZedBoard-Preset + AXI-GPIO (LEDs), Bitstream, `system.xsa` |
| `build_petalinux.sh`| PetaLinux-Projekt aus XSA, ext4-Rootfs auf SD, Build, `BOOT.BIN`, `.wic` |
| `system-user.dtsi`  | Device-Tree-Ergänzungen (Konsole, Ethernet-PHY, SD)          |
| `prepare_sd.sh`     | SD-Karte manuell partitionieren und befüllen                 |

## Voraussetzungen

- Linux-Host, den die jeweilige PetaLinux-Version offiziell unterstützt (siehe UG1144 der Version)
- Vivado und PetaLinux **in derselben Version** (z. B. beide 2024.2)
- ZedBoard-Boardfiles (das Tcl-Skript versucht sie aus dem Board Store zu installieren)
- SD-Karte ≥ 4 GB

## Ablauf

```bash
# 1. Hardware
source /tools/Xilinx/Vivado/<ver>/settings64.sh
vivado -mode batch -source create_hw.tcl            # -> system.xsa

# 2. Software
source /tools/petalinux/<ver>/settings.sh
./build_petalinux.sh ./system.xsa zedboard-plnx     # dauert beim ersten Mal lange

# 3a. SD-Karte – schnell, per Image
sudo dd if=zedboard-plnx/images/linux/petalinux-sdimage.wic of=/dev/sdX bs=4M conv=fsync status=progress

# 3b. SD-Karte – manuell
sudo ./prepare_sd.sh /dev/sdX zedboard-plnx/images/linux
```

## Jumper für SD-Boot (Boot-Mode-Jumper JP7–JP11)

| Jumper | Stellung |
|--------|----------|
| JP7    | GND      |
| JP8    | GND      |
| JP9    | 3V3      |
| JP10   | 3V3      |
| JP11   | GND      |

Zum Vergleich mit dem Schaltplan: ZedBoard Hardware User's Guide, Abschnitt „Configuration Modes".

## Erster Boot

- Serielle Konsole an **J14 (USB-UART)**, 115200 8N1, z. B. `picocom -b 115200 /dev/ttyACM0`
- Blaue LED **LD12 (DONE)** leuchtet, sobald der Bitstream geladen ist
- Login: User `petalinux`, Passwort wird beim ersten Login gesetzt
- LED-Test (AXI-GPIO): `ls /sys/class/gpio/` – den `gpiochip` mit 8 GPIOs suchen, oder per `gpioset` aus libgpiod

## Typische Anpassungen

- **Kernel-Optionen:** `petalinux-config -c kernel`
- **U-Boot:** `petalinux-config -c u-boot`
- **Pakete im Rootfs:** `petalinux-config -c rootfs`
- **Neue PL-Hardware:** Vivado ändern → XSA neu exportieren → `petalinux-config --get-hw-description=<xsa> --silentconfig` → `petalinux-build` → `petalinux-package boot ...`

## Fehlersuche

| Symptom                                   | Ursache / Prüfen |
|-------------------------------------------|------------------|
| Gar keine Ausgabe auf UART                | Jumper, FAT32-Partition, `BOOT.BIN` im Root der FAT-Partition |
| Hängt nach FSBL                           | Bitstream passt nicht zur XSA (immer gemeinsam neu bauen) |
| U-Boot startet, Kernel nicht              | `boot.scr`/`image.ub` fehlen; in U-Boot `run distro_bootcmd` bzw. `fatls mmc 0` |
| `VFS: Unable to mount root fs`            | Rootfs nicht auf `mmcblk0p2`, oder Image noch als initramfs konfiguriert |
| Ethernet ohne Link                        | PHY-Knoten in `system-user.dtsi`, `phy-mode` prüfen |

## Hinweis zu PetaLinux-Versionen

Ab 2024.1 nutzt PetaLinux die neue Kommandosyntax (`petalinux-create project`, `petalinux-package boot`).
Die Skripte probieren zuerst die neue und fallen dann auf die alte Syntax zurück.
AMD verlagert seine Embedded-Linux-Flows schrittweise Richtung Yocto-basierter Tools – prüfe für 2025.x
in den Release Notes, ob PetaLinux für deine Version noch der empfohlene Weg ist.
