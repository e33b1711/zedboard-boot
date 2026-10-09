# ZedBoard – Boot-Konfiguration from scratch (Vivado + PetaLinux, SD-Boot)

Getestet mit Vivado 2025.2 und PetaLinux 2025.2 auf Linux Mint 22.3 (Ubuntu-24.04-Basis):
Board bootet von SD bis zum Login, Ethernet funktioniert, Sinuston auf Line Out ist hörbar.

## Boot-Kette

```
BootROM ──► FSBL ──► Bitstream (PL) ──► U-Boot ──► boot.scr ──► Kernel + DTB ──► rootfs (ext4, mmcblk0p2)
           └──────── BOOT.BIN (inkl. DTB) ────────┘    └─── FAT-Partition 1 ───┘
```

`boot.scr` lädt `image.ub` (FIT: Kernel + DTB), falls vorhanden. Das `.wic`-Image enthält stattdessen
`uImage`; der Device Tree kommt dann aus `BOOT.BIN` (dort auf `0x100000` abgelegt).

## Dateien

| Datei               | Zweck                                                        |
|---------------------|--------------------------------------------------------------|
| `build_hls.tcl`     | Vitis HLS: Sinus-Generator `hls/sine_gen.cpp` → C-Simulation, Synthese, Vivado-IP |
| `create_hw.tcl`     | Vivado-Projekt: PS7 mit ZedBoard-Preset, AXI-GPIO (LEDs), Sinuston-Kette, Bitstream, `system.xsa` |
| `rtl/i2s_tx.v`      | I2S-Sender (Verilog) für den Audio-Codec; Testbench in `sim/` |
| `constraints/audio.xdc` | Pins des Audio-Codecs ADAU1761 |
| `build_petalinux.sh`| PetaLinux-Projekt aus XSA, ext4-Rootfs auf SD, Build, `BOOT.BIN`, `.wic` |
| `target/`           | Skripte fürs Board (`codec_init.sh`, `set_freq.sh`) und ihr Rezept; landen in `/usr/bin` |
| `system-user.dtsi`  | Device-Tree-Ergänzungen (Konsole, Ethernet-PHY, SD)          |
| `prepare_sd.sh`     | SD-Karte manuell partitionieren und befüllen (bisher nicht getestet) |

## Voraussetzungen

- Linux-Host, den die jeweilige PetaLinux-Version offiziell unterstützt (siehe UG1144 der Version).
  Mint 22.3 wird als „not a supported OS" gemeldet, funktioniert aber.
- Vivado und PetaLinux **in derselben Version** (hier beide 2025.2)
- ZedBoard-Boardfiles (das Tcl-Skript installiert sie bei Bedarf aus dem Board Store, braucht dafür Internet)
- SD-Karte ≥ 8 GB für das `.wic`-Image (6,1 GB: 2 GB FAT + 4 GB ext4); für `prepare_sd.sh` reichen 4 GB

### Host-Pakete (Mint 22 / Ubuntu 24.04)

Vivado braucht `libncurses.so.5`, das es in den Paketquellen nicht mehr gibt
(Fehler: `couldn't load file "libxv_commontasks.so": libncurses.so.5`). Pakete aus Ubuntu 22.04 nachinstallieren:

```bash
cd /tmp
wget http://security.ubuntu.com/ubuntu/pool/universe/n/ncurses/libtinfo5_6.3-2ubuntu0.3_amd64.deb
wget http://security.ubuntu.com/ubuntu/pool/universe/n/ncurses/libncurses5_6.3-2ubuntu0.3_amd64.deb
sudo apt install ./libtinfo5_6.3-2ubuntu0.3_amd64.deb ./libncurses5_6.3-2ubuntu0.3_amd64.deb
```

PetaLinux (der Installer bricht ohne `xterm`, `autoconf`, `libtool` ab):

```bash
sudo apt install xterm autoconf libtool libtool-bin automake gawk flex bison texinfo \
  diffstat chrpath screen pax libssl-dev libsdl1.2-dev tftpd-hpa dnsutils \
  python3-git python3-jinja2 pylint
```

PetaLinux-Installation (nicht als root; `--platform "arm"` = nur Zynq-7000):

```bash
<installer>.run --dir <petalinux-install> --platform "arm"
```

## Ablauf

Installationspfade auf diesem Rechner: Vivado `~/tools/amd/2025.2/Vivado`, PetaLinux `~/tools/amd/petalinux`.

```bash
# 1. HLS-IP und Hardware (je ca. 3 min)
source ~/tools/amd/2025.2/Vitis/settings64.sh
vitis-run --mode hls --tcl build_hls.tcl            # -> hls/proj/solution1/impl/ip
source ~/tools/amd/2025.2/Vivado/settings64.sh
vivado -mode batch -source create_hw.tcl            # -> system.xsa

# 2. Software (erster Build ca. 30 min, lädt viel herunter)
source ~/tools/amd/petalinux/settings.sh
bash ./build_petalinux.sh ./system.xsa zedboard-plnx

# 3a. SD-Karte – per Image (Gerätename vorher mit lsblk prüfen!)
IMG=zedboard-plnx/images/linux/petalinux-sdimage.wic
sudo dd if=$IMG of=/dev/sdX bs=4M oflag=direct conv=fsync status=progress
sudo cmp -n $(stat -c %s $IMG) $IMG /dev/sdX && echo CARD OK      # Karte verifizieren

# 3b. SD-Karte – manuell
sudo bash ./prepare_sd.sh /dev/sdX zedboard-plnx/images/linux
```

Hinweise:

- `oflag=direct` schreibt am Cache vorbei. Ohne diese Option zeigt `dd` sofort 100 % an und wirkt danach
  minutenlang „hängend", während im Hintergrund noch geschrieben wird – Karte dann nicht ziehen.
- Das `cmp` nach dem Schreiben lohnt sich: eine defekte Karte fällt sonst erst beim Booten auf.
- Die Warnungen von PetaLinux zu `/bin/sh is not bash`, „not a supported OS", Deprecation und
  `/tftpboot` sind unkritisch.

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

- Serielle Konsole an **J14 (USB-UART)**, 115200 8N1: `sudo screen /dev/ttyACM0 115200`
  (beenden mit `Ctrl-A`, `K`, `y`). Ohne sudo: `sudo usermod -aG dialout $USER`, danach neu anmelden.
- Blaue LED **LD12 (DONE)** leuchtet, sobald der Bitstream geladen ist
- Login: User `petalinux`, Passwort wird beim ersten Login gesetzt

## PL-Peripherie

| Peripherie        | Anbindung |
|-------------------|-----------|
| LEDs LD0–LD7      | AXI-GPIO `leds` auf `0x41200000`, 8 Bit, nur Ausgang |
| Sinus-Generator   | HLS-Core `sine_gen` auf `0x40000000`, Register `phase_inc` bei Offset `0x10` |
| Audio-Codec ADAU1761 | I2S aus `i2s_tx`, I2C über PS-I2C0 (EMIO), Adresse `0x38` |
| DIP-Schalter, Taster | nicht angeschlossen (Board-Interfaces `sws_8bits`, `btns_5bits` wären verfügbar) |

LED-Test über sysfs (als root; `gpioset`/libgpiod ist nicht im Rootfs):

```bash
grep -l 41200000 /sys/class/gpio/gpiochip*/label     # -> gpiochipN
BASE=$(cat /sys/class/gpio/gpiochipN/base)
echo $BASE > /sys/class/gpio/export
echo out > /sys/class/gpio/gpio$BASE/direction
echo 1 > /sys/class/gpio/gpio$BASE/value             # LD0 an; LD1..LD7 = BASE+1..BASE+7
```

## Sinuston auf Line Out

```
Linux ──AXI-Lite (phase_inc)──► sine_gen (HLS) ──AXI-Stream, 24 Bit──► Clock Converter ──► i2s_tx ──► ADAU1761 ──► Line Out
PS-I2C0 (EMIO) ────────────────────────────────────────────────────────────── Codec-Register ──┘
Clocking Wizard: 100 MHz ──► 12,288 MHz (MCLK = 256 × 48 kHz)
```

- `sine_gen` ist ein DDS: 32-Bit-Phasenakkumulator, Viertelwellen-Tabelle mit 1024 Einträgen, Pegel −6 dBFS.
  `f = phase_inc × 48000 / 2^32` (Auflösung ca. 0,011 Hz). Nach dem Reset ist `phase_inc = 0` = Stille.
- Der Core läuft frei mit 100 MHz; `i2s_tx` nimmt pro 48-kHz-Frame genau ein Sample ab und gibt es
  links und rechts aus. Der FPGA ist I2S-Master, der Codec Slave ohne PLL.
- Der Codec muss nach jedem Boot einmal per I2C eingerichtet werden.

Auf dem Board (als root):

```bash
codec_init.sh        # Codec einrichten: Line Out 0 dB, Kopfhörer −20 dB
set_freq.sh 440      # 440 Hz
set_freq.sh          # aktuelle Frequenz anzeigen
set_freq.sh 0        # Stille
```

Tests ohne Board:

```bash
vitis-run --mode hls --tcl build_hls.tcl    # enthält die C-Simulation von sine_gen ("TEST OK")
bash sim/run_sim.sh                         # i2s_tx im Vivado-Simulator ("TEST OK")
```

## Typische Anpassungen

- **Kernel-Optionen:** `petalinux-config -c kernel`
- **U-Boot:** `petalinux-config -c u-boot`
- **Pakete im Rootfs:** `petalinux-config -c rootfs`
- **Neue PL-Hardware:** Vivado ändern → XSA neu exportieren → `build_petalinux.sh` erneut aufrufen (importiert die XSA in das bestehende Projekt, baut und packt neu)

## Fehlersuche

| Symptom                                   | Ursache / Prüfen |
|-------------------------------------------|------------------|
| Vivado: `libncurses.so.5: cannot open shared object file` | `libncurses5`/`libtinfo5` fehlen, siehe Host-Pakete |
| `create_hw.tcl`: „Keine ZedBoard-Boardfiles verfuegbar" | Im Batch-Modus durchsucht Vivado den Board Store nicht von selbst; das Skript setzt dafür `board.repoPaths`. Sonst: kein Internet für den Download? |
| `dd`: 100 %, danach scheinbar Stillstand  | Cache wird noch geschrieben; `oflag=direct` verwenden. Bei `Medium not present` in `dmesg` ist die Karte/der Leser ausgestiegen |
| Gar keine Ausgabe auf UART                | Jumper, FAT32-Partition, `BOOT.BIN` im Root der FAT-Partition |
| Hängt nach FSBL                           | Bitstream passt nicht zur XSA (immer gemeinsam neu bauen) |
| U-Boot: `Verifying Checksum ... Bad Data CRC` | Daten auf der SD-Karte beschädigt: Karte mit `cmp` gegen das Image prüfen, andere Karte nehmen |
| U-Boot startet, Kernel nicht              | `boot.scr` bzw. `image.ub`/`uImage` fehlen; in U-Boot `fatls mmc 0:1`, `boot` |
| `VFS: Unable to mount root fs`            | Rootfs nicht auf `mmcblk0p2`, oder Image noch als initramfs konfiguriert |
| Ethernet ohne Link                        | PHY-Knoten in `system-user.dtsi`, `phy-mode` prüfen |
| `xelab`: `cannot find crt1.o`             | Vivado-Simulator findet die System-Bibliotheken nicht: `export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu` (steht in `sim/run_sim.sh`) |
| `codec_init.sh`: „Codec antwortet nicht"  | Bitstream geladen (ohne MCLK reagiert der Codec nicht)? `i2cdetect -y <bus>` muss `38` zeigen |

## Hinweis zu PetaLinux-Versionen

Ab 2024.1 nutzt PetaLinux die neue Kommandosyntax (`petalinux-create project`, `petalinux-package boot`).
Die Skripte probieren zuerst die neue und fallen dann auf die alte Syntax zurück.
PetaLinux 2025.2 meldet selbst, dass das Toolset mit 2026.2 abgekündigt wird; Nachfolger ist AMD EDF
mit Yocto-basiertem Workflow.
