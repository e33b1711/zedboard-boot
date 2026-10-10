# ZedBoard – Boot-Konfiguration from scratch (Vivado + PetaLinux, SD-Boot)

Getestet mit Vivado 2025.2 und PetaLinux 2025.2 auf Linux Mint 22.3 (Ubuntu-24.04-Basis):
Board bootet von SD bis zum Login, Ethernet funktioniert, Sinuston auf Line Out ist hörbar,
HDMI-Testbilder in 1080p60 auf dem Monitor geprüft (Farben korrekt mit `format 0`, Farbverlauf ohne Pixelfehler).
Die Framebuffer-Ausgabe (Testbild 7, `fb_test`) ist auf dem Monitor geprüft: ruhiges Bild, die drei Puffer wechseln sauber.

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
| `build_hls.tcl`     | Vitis HLS: `hls/sine_gen.cpp` (Sinus) und `hls/pattern_gen.cpp` (HDMI-Testbild, Framebuffer-Leser) → C-Simulation, Synthese, Vivado-IP |
| `cosim_hls.tcl`     | RTL-Co-Simulation von `pattern_gen` mit kleinem Bild (prüft den Framebuffer-Leser am AXI-Master) |
| `create_hw.tcl`     | Vivado-Projekt: PS7 mit ZedBoard-Preset, AXI-GPIO (LEDs), Sinuston- und HDMI-Kette, Framebuffer-Anbindung, Bitstream, `system.xsa` |
| `rtl/i2s_tx.v`      | I2S-Sender (Verilog) für den Audio-Codec; Testbench in `sim/` |
| `rtl/clk_fwd.v`     | Takt-Ausgabe über ODDR (HDMI-Pixeltakt) |
| `constraints/audio.xdc`, `hdmi.xdc` | Pins des Audio-Codecs ADAU1761 und des HDMI-Senders ADV7511 |
| `build_petalinux.sh`| PetaLinux-Projekt aus XSA, ext4-Rootfs auf SD, Build, `BOOT.BIN`, `.wic` |
| `target/`           | Werkzeuge fürs Board (`codec_init.sh`, `set_freq.sh`, `hdmi_init.sh`, `set_pattern.sh`, `fb_test.c`) und ihr Rezept; landen in `/usr/bin` |
| `system-user.dtsi`  | Device-Tree-Ergänzungen (Konsole, Ethernet-PHY, SD, reservierter Speicher für die Framebuffer) |
| `prepare_sd.sh`     | SD-Karte manuell partitionieren und befüllen (bisher nicht getestet) |
| `fb_rennrad.py`     | Testbild „Rennrad" auf dem PC zeichnen (Python, Pillow) und über `board.sh` in einen Framebuffer laden |
| `board.sh`          | Board über das Netzwerk aktualisieren und bedienen (SSH), statt die Karte neu zu schreiben (getestet: Board finden, Befehle, Werkzeuge übertragen; noch nicht: geänderte Boot-Dateien mit Neustart) |

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
# 1. HLS-IPs (ca. 5 min) und Hardware (ca. 4 min)
source ~/tools/amd/2025.2/Vitis/settings64.sh
vitis-run --mode hls --tcl build_hls.tcl            # -> hls/proj_<core>/solution1/impl/ip
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

## Aktualisieren über das Netzwerk

Die Karte muss nur einmal mit einem Image geschrieben werden, das den SSH-Zugang enthält. Danach:

```bash
./board.sh deploy          # Werkzeuge und geänderte Boot-Dateien übertragen, bei Bedarf Neustart
./board.sh tools           # nur Skripte und fb_test nach /usr/bin, ohne Neustart
./board.sh run fb_test     # Befehl auf dem Board ausführen (als root)
./board.sh find            # Adresse des Boards
```

- `build_petalinux.sh` legt beim ersten Lauf einen Host-Schlüssel in `./keys` an (nicht im Git) und baut ihn
  zusammen mit dem öffentlichen Schlüssel des PCs (`~/.ssh/id_ed25519.pub`, sonst `ZED_SSH_PUBKEY=...`) ins Image.
  Der Zugang als root übersteht damit auch ein neu geschriebenes Image.
- `board.sh` erkennt das Board an diesem Host-Schlüssel: erst unter der zuletzt benutzten Adresse, dann unter dem
  Namen `zedboard-plnx`, sonst durchsucht es das eigene /24-Netz. Eine wechselnde IP-Adresse stört also nicht.
  Feste Adresse vorgeben: `ZED_HOST=192.168.178.60 ./board.sh ...`.
- `deploy` vergleicht die Dateien der FAT-Partition (`BOOT.BIN`, `boot.scr`, `uImage`) per Prüfsumme, überträgt nur
  geänderte, prüft sie auf der Karte nach und ersetzt die alte Datei erst danach.
- Nicht übertragen wird der Rest des Rootfs (Kernel-Module, Pakete). Nach Änderungen an der Kernel-Konfiguration
  oder an den Paketen die Karte neu schreiben. Das gilt auch, wenn ein Image nicht mehr bootet.
- Die MAC-Adresse steht fest im Device Tree (`00:0a:35:00:1e:53`, Vorgabe von PetaLinux) und kommt so auf dem
  Board an; die Schnittstelle heißt dort `enx000a35001e53`.

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
| Testbild-Generator | HLS-Core `pattern_gen` auf `0x40010000`, Register `pattern` `0x10`, `color` `0x18`, `format` `0x20`, `fb_addr0..2` `0x28`/`0x30`/`0x38`, `fb_frames` `0x40` |
| Framebuffer       | 3 × 4 MiB im DDR auf `0x1E000000`, `0x1E400000`, `0x1E800000`; der Core liest sie über den HP0-Port des PS |
| HDMI-Sender ADV7511 | 16 Bit YCbCr 4:2:2 + Syncs aus `video_out`, I2C über PS-I2C1 (EMIO), Adresse `0x39` |
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
vitis-run --mode hls --tcl build_hls.tcl    # enthält die C-Simulationen beider HLS-Cores ("TEST OK")
bash sim/run_sim.sh                         # i2s_tx im Vivado-Simulator ("TEST OK")
```

## Testbild auf HDMI

```
Linux ──AXI-Lite (pattern, color)──► pattern_gen (HLS) ──AXI-Stream-Video──► AXI4-Stream to Video Out ──► ADV7511 ──► HDMI
                                                          Video Timing Controller ──┘ (HSYNC, VSYNC, DE)
PS-I2C1 (EMIO) ──────────────────────────────────────────────────────── ADV7511-Register ──┘
Clocking Wizard: 100 MHz ──► 148,5 MHz Pixeltakt
```

- Feste Auflösung 1920×1080 bei 60 Hz. Für Linux gibt es kein Display: kein Treiber, kein Framebuffer,
  kein Device-Tree-Knoten für den ADV7511. Gesteuert wird nur über die beiden Skripte.
- `pattern_gen` erzeugt pro Takt ein Pixel, rechnet das Testbild in RGB und gibt es als
  YCbCr 4:2:2 (BT.709, begrenzter Wertebereich) aus, weil auf dem ZedBoard nur 16 Datenleitungen
  zum ADV7511 führen. Der ADV7511 wandelt wieder nach RGB.
- Registeränderungen werden am Bildanfang übernommen, das Bild wechselt also nie mittendrin.
- Der ADV7511 muss nach jedem Boot einmal per I2C eingerichtet werden. Das geht auch ohne Monitor;
  späteres An- und Umstecken funktioniert ohne erneutes `hdmi_init.sh` (getestet).

| `pattern` | Testbild |
|-----------|----------|
| 0 | schwarz (Zustand nach Reset) |
| 1 | 8 Farbbalken: weiß, gelb, cyan, grün, magenta, rot, blau, schwarz |
| 2 | Graukeil horizontal |
| 3 | Schachbrett, 64 Pixel |
| 4 | Vollfläche in der Farbe aus `color` (`0xRRGGBB`) |
| 5 | weißes Quadrat, das über dunkelblauen Hintergrund wandert |
| 6 | Farbverlauf (rot horizontal, grün vertikal, blau diagonal), der mit der Zeit wandert |
| 7 | Framebuffer 0, 1, 2 im Wechsel (siehe unten) |

Auf dem Board (als root):

```bash
hdmi_init.sh               # ADV7511 einrichten, meldet ob ein Monitor erkannt wurde
set_pattern.sh 1           # Farbbalken
set_pattern.sh 4 ff8000    # Vollfläche orange
set_pattern.sh             # aktuelle Einstellung anzeigen
```

Timing: Der Pixeltakt von 148,5 MHz ist für diesen FPGA (Speed Grade -1) knapp, der Build schließt
mit ca. 0,6 ns Reserve. Die HDMI-Pins haben wie im ADI-Referenzdesign keine Output-Delay-Constraints;
Ausgangsregister in den IO-Zellen und der per ODDR ausgegebene Takt halten die Laufzeiten gleich.
Für 720p: `FRAME_WIDTH`/`FRAME_HEIGHT` in `hls/pattern_gen.h`, Taktperiode in `build_hls.tcl`,
Pixeltakt (74,25 MHz) und `VIDEO_MODE` in `create_hw.tcl` ändern.

Zur Fehlersuche an der Datenstrecke lässt sich die Byte-Reihenfolge ohne Neubau ändern:
`set_pattern.sh format N` mit Bit 0 = Cb/Cr tauschen, Bit 1 = Y und Cb/Cr tauschen.

Die C-Simulation schreibt alle Testbilder als `pattern<n>.ppm` nach
`hls/proj_pattern_gen/solution1/csim/build/`.

## Framebuffer

```
fb_test ──/dev/mem──► 3 Framebuffer im DDR ──HP0 (AXI3, 100 MHz)──► SmartConnect ──AXI4, 148,5 MHz──► pattern_gen ──► wie oben
Linux ──AXI-Lite (fb_addr0..2, fb_frames, pattern = 7)──────────────────────────────────────────────┘
```

- Mit Testbild 7 gibt `pattern_gen` statt eines gerechneten Bildes den Inhalt von drei Framebuffern aus,
  reihum 0, 1, 2. Jeder Puffer bleibt `fb_frames` Bilder lang stehen (0 wirkt wie 1; 60 = eine Sekunde).
- Speicherformat ist das Format der HDMI-Strecke: 1920×1080, 2 Byte pro Pixel in der Reihenfolge
  Y, Cb, Y, Cr („YUYV“, BT.709, begrenzter Wertebereich), zeilenweise ohne Lücken, 4 147 200 Byte pro Bild.
  Die Wandlung aus RGB macht `fb_test` auf der CPU.
- Die drei Bereiche stehen in `system-user.dtsi` unter `reserved-memory` mit `no-map`; Linux hat dadurch
  12 MB weniger Speicher. `fb_test` liest die Adressen aus dem Device Tree und trägt sie in den Core ein.
  Vor dem ersten `fb_test` stehen die Adressregister auf 0, Testbild 7 zeigt dann Speicherinhalt ab Adresse 0.
- Im Core liest ein eigener Prozess den Puffer in Bursts (64 Bit, 16 Worte) in ein FIFO mit gut einer Zeile
  Vorrat. Die Register werden einmal pro Bild übernommen; eine Änderung wirkt nach bis zu drei Bildern.
- Für Linux gibt es weiterhin kein Display. Die Puffer sind reiner Speicher, kein `/dev/fb0`.

Auf dem Board (als root, nach `hdmi_init.sh`):

```bash
fb_test                    # alle drei Puffer füllen, Adressen eintragen, Testbild 7 einschalten
set_pattern.sh 7 30        # Wechsel alle 30 Bilder (0,5 s)
fb_test 1 balken           # Puffer 1 neu füllen, während die Ausgabe läuft
fb_test 0 farbe ff8000     # Vollfläche orange in Puffer 0
fb_test 2 foto.ppm         # eigenes Bild (binäres PPM, wird eingepasst)
```

| Bild | Inhalt |
|------|--------|
| `stern`   | neunzackiger Stern, rot auf schwarz (Vorgabe für Puffer 0) |
| `barcode` | Strichcode 742617000027 als UPC-A, 16 Pixel pro Modul; harte Schwarz-Weiß-Kanten (Vorgabe für Puffer 1) |
| `schrift` | Schriftzug mit Zeilenversatz und Farbsäumen; zeigt die halbierte Farbauflösung von 4:2:2 (Vorgabe für Puffer 2) |
| `balken`, `verlauf` | Farbbalken wie Testbild 1, Farbverlauf |
| `ziffer`  | Puffernummer bildschirmfüllend |
| `farbe RRGGBB` | Vollfläche |

Alle Bilder außer `farbe` und `ziffer` tragen unten rechts die Puffernummer (`FB0` bis `FB2`).
Ein eigenes Bild auf dem PC nach PPM wandeln: `ffmpeg -i foto.jpg foto.ppm` oder `convert foto.jpg foto.ppm`.

Vom PC aus lädt `./fb_rennrad.py [0|1|2]` ein dort gezeichnetes Bild (Rennrad vor Abendhimmel) in einen Puffer,
Vorgabe ist Puffer 1. Es nimmt den Weg über eine PPM-Datei und `fb_test N datei.ppm` und dient als Vorlage
für eigene Bilder vom PC.

Tests ohne Board:

```bash
vitis-run --mode hls --tcl build_hls.tcl    # C-Simulation: Reihenfolge, Wechseltakt, Daten bytegenau ("TEST OK")
vitis-run --mode hls --tcl cosim_hls.tcl    # RTL-Co-Simulation, Bild 128x16 ("co-simulation finished: PASS")
gcc -O2 target/fb_test.c -o /tmp/fb_test -lm && /tmp/fb_test -o /tmp    # Bilder als /tmp/fb0.ppm .. fb2.ppm
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
| HDMI: kein Bild                           | `hdmi_init.sh` gelaufen? Meldet es „Monitor erkannt"? Monitor muss 1080p60 können |
| HDMI: falsche Farben (grün/rosa, rot/blau vertauscht) | Byte-Reihenfolge: `set_pattern.sh format 1`, `2` oder `3` probieren, mit Farbbalken prüfen |
| HDMI: flimmernde Einzelpixel              | Takt/Daten-Lage: Taktverzögerung im ADV7511 (Register `0xBA`) in `hdmi_init.sh` ändern |
| Testbild 7: Bild steht, wechselt nicht       | `fb_frames` sehr groß? `set_pattern.sh` zeigt den Wert, `set_pattern.sh 7 60` setzt ihn |
| Testbild 7: Rauschen oder fremder Bildinhalt | Adressregister nicht gesetzt (`fb_test` einmal ohne Argumente starten) oder Puffer noch nie beschrieben |
| Testbild 7: Bild läuft durch, reißt oder Monitor verliert das Signal | Der Leser kommt nicht nach (FIFO läuft leer). Tritt es bei Testbild 1 nicht auf, liegt es an der Speicherstrecke |
| `fb_test`: „nicht genau 3 Knoten reserved-memory/framebuffer@..." | Device Tree ohne die Ergänzung aus `system-user.dtsi` gebaut; `ls /proc/device-tree/reserved-memory` |
| `fb_test`: „Ruecklesen stimmt nicht"          | Speicherbereich wird doch von Linux benutzt: `cat /proc/iomem` darf dort kein „System RAM" zeigen |
| `codec_init.sh`: „Codec antwortet nicht"  | Bitstream geladen (ohne MCLK reagiert der Codec nicht)? `i2cdetect -y <bus>` muss `38` zeigen |

## Hinweis zu PetaLinux-Versionen

Ab 2024.1 nutzt PetaLinux die neue Kommandosyntax (`petalinux-create project`, `petalinux-package boot`).
Die Skripte probieren zuerst die neue und fallen dann auf die alte Syntax zurück.
PetaLinux 2025.2 meldet selbst, dass das Toolset mit 2026.2 abgekündigt wird; Nachfolger ist AMD EDF
mit Yocto-basiertem Workflow.
