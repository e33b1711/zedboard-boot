# ZedBoard – Boot-Konfiguration from scratch (Vivado + PetaLinux, SD-Boot)

Getestet mit Vivado 2025.2 und PetaLinux 2025.2 auf Linux Mint 22.3 (Ubuntu-24.04-Basis):
Board bootet von SD bis zum Login, Ethernet funktioniert, Sinuston auf Line Out ist hörbar,
HDMI-Testbilder in 1080p60 auf dem Monitor geprüft (Farben korrekt mit `format 0`, Farbverlauf ohne Pixelfehler).
Die Framebuffer-Ausgabe (Testbild 7, `fb_test`) ist auf dem Monitor geprüft: ruhiges Bild, die drei Puffer wechseln sauber.
Die Linien (Testbild 8) sind auf dem Monitor geprüft, als festes Bild und bildgenau bewegt von `vec_demo`.

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
| `build_hls.tcl`     | Vitis HLS: `hls/sine_gen.cpp` (Sinus), `hls/pattern_gen.cpp` (HDMI-Testbild, Framebuffer-Leser, Linien), `hls/iq_reader.cpp` (Leser für den I/Q-Ringpuffer) und `hls/iq_spectrum.cpp` (FFT, Mittelung, Max-Hold) → C-Simulation, Synthese, Vivado-IP |
| `hls/gen_lines.py`  | erzeugt das feste Linienbild für Testbild 8 (`hls/lines_rom.h`) |
| `target/lines.h`    | Eintrag der Linientabelle aus Endpunkten rechnen und am Bildrand beschneiden (von `vec_demo` und der Testbench von `pattern_gen` benutzt) |
| `cosim_hls.tcl`     | RTL-Co-Simulation von `pattern_gen` (kleines Bild) und `iq_reader`; prüft die Leser am AXI-Master |
| `create_hw.tcl`     | Vivado-Projekt: PS7 mit ZedBoard-Preset, AXI-GPIO (LEDs), Sinuston- und HDMI-Kette, Framebuffer-Anbindung, Bitstream, `system.xsa` |
| `rtl/i2s_tx.v`      | I2S-Sender (Verilog) für den Audio-Codec; Testbench in `sim/` |
| `rtl/clk_fwd.v`     | Takt-Ausgabe über ODDR (HDMI-Pixeltakt) |
| `rtl/iq_scope.v`    | I/Q-Oszilloskop als Einblendung im Videostrom, Bildspeicher im Block-RAM; Testbench in `sim/` |
| `rtl/spec_trace.v`  | zeichnet das Spektrum als Kurve in das Oszilloskop; Testbench in `sim/` |
| `rtl/irq_timer.v`   | misst die Laufzeit eines Interrupts von der PL bis zur Antwort von Linux (AXI-Lite, Statistik in der PL); Testbench in `sim/` |
| `rtl/oled_mux.v`    | Leitungen der OLED-Anzeige: Takt und Daten vom SPI-Core oder zur Fehlersuche von Hand aus dem Steuerwort |
| `rtl/frame_irq.v`   | macht aus jedem Pegelwechsel von `frame_tick` (`pattern_gen`, einmal pro Bild) einen Interrupt-Puls für `IRQ_F2P[1]` |
| `rtl/gen_palette.py`| erzeugt die Farbtabellen des Oszilloskops (`rtl/iq_palette.v`) |
| `constraints/audio.xdc`, `hdmi.xdc`, `oled.xdc` | Pins des Audio-Codecs ADAU1761, des HDMI-Senders ADV7511 und der OLED-Anzeige |
| `build_petalinux.sh`| PetaLinux-Projekt aus XSA, ext4-Rootfs auf SD, Build, `BOOT.BIN`, `.wic` |
| `target/`           | Werkzeuge fürs Board (`codec_init.sh`, `set_freq.sh`, `hdmi_init.sh`, `set_pattern.sh`, `set_scope.sh`, `fb_test.c`, `iq_rx.c`, `irq_test.c`, `oled.c`, `vec_demo.c`) und ihr Rezept; landen in `/usr/bin` |
| `target/oled.h`, `oled_font.h` | Ansteuerung der OLED-Anzeige (von `oled.c` und `irq_test.c` eingebunden) und ihr Zeichensatz, erzeugt von `target/gen_oled_font.py` |
| `target/hackrf.bb`  | Rezept für `libhackrf` und die HackRF-Werkzeuge (`hackrf_info`, `hackrf_transfer`, `hackrf_debug`, …) |
| `system-user.dtsi`  | Device-Tree-Ergänzungen (Konsole, Ethernet-PHY, SD, reservierter Speicher für die Framebuffer und den I/Q-Ringpuffer, `irq_timer` als UIO-Gerät, SPI-Core der OLED-Anzeige abgeschaltet) |
| `prepare_sd.sh`     | SD-Karte manuell partitionieren und befüllen (bisher nicht getestet) |
| `fb_rennrad.py`     | Testbild „Rennrad" auf dem PC zeichnen (Python, Pillow) und über `board.sh` in einen Framebuffer laden |
| `hackrf_messreihe.sh` | Empfang vom HackRF bei steigender Abtastrate messen (startet `iq_rx` über `board.sh`), Tabelle mit CPU-Last und Lücken |
| `board.sh`          | Board über das Netzwerk aktualisieren und bedienen (SSH), statt die Karte neu zu schreiben (getestet, auch mit geändertem `BOOT.BIN` und Neustart) |

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
./board.sh tools           # nur Skripte und Programme nach /usr/bin, dazu die HackRF-Bibliotheken; ohne Neustart
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
| Testbild-Generator | HLS-Core `pattern_gen` auf `0x40010000`, Register `pattern` `0x10`, `color` `0x18`, `format` `0x20`, `fb_addr0..2` `0x28`/`0x30`/`0x38`, `fb_frames` `0x40`, `lines_ctrl` `0x48`, `lines_cur` `0x50`, `frames` `0x60`, Linientabelle ab `0x400`; Puls am Bildanfang an `IRQ_F2P[1]` (SPI 62, `<0 30 1>`), als UIO-Gerät |
| Framebuffer       | 3 × 4 MiB im DDR auf `0x1E000000`, `0x1E400000`, `0x1E800000`; der Core liest sie über den HP0-Port des PS |
| I/Q-Ringpuffer    | 16 MiB im DDR auf `0x1F000000`; HLS-Core `iq_reader` liest ihn über den HP1-Port des PS und speist das Oszilloskop. Register: `enable` `0x10`, `ring_addr` `0x18`, `ring_bytes` `0x20`, `wr_ptr` `0x28`, `rd_ptr` `0x30`, `samples` `0x40`, `seq_errors` `0x50` |
| Spektrum          | HLS-Core `iq_spectrum` (FFT 1024, Blockmittelung, Max-Hold) bekommt dieselben I/Q-Daten; Register `avg_log2` `0x10`, `maxhold` `0x18`, `spectra` `0x20`, `overflows` `0x30`. `spec_trace` gibt das Ergebnis als Kurve an das Oszilloskop |
| I/Q-Oszilloskop   | Verilog-Modul `iq_scope` im Videostrom hinter `pattern_gen`; Steuerwort über AXI-GPIO `scope_ctrl` (32 Bit, nur Ausgang, Adresse siehe `set_scope.sh`) |
| Interrupt-Laufzeit | Verilog-Modul `irq_timer` auf `0x40050000` an FCLK0 (100 MHz), IRQ an `IRQ_F2P[0]` des PS (SPI 61, im Device Tree `<0 29 4>`); Register siehe unten |
| OLED-Anzeige (SSD1306, 128 × 32) | AXI Quad SPI `oled_spi` auf `0x44A00000` für Takt und Daten (3,125 MHz), AXI-GPIO `oled_ctrl` auf `0x40060000` mit 7 Ausgängen: Bit 0 D/C, 1 RES, 2 VBAT, 3 VDD (bei 0 aktiv), Bit 4 bis 6 Takt und Daten von Hand (`rtl/oled_mux.v`) |
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
| 8 | 32 Linien, grün auf schwarz (siehe unten) |

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

### Linien (Testbild 8)

Der Core zeichnet bis zu 32 Linien direkt im Pixeltakt, ohne Bildspeicher. Die Linien kommen
entweder aus dem festen Bild (Rahmen, Raute und ein Stern mit 24 Strahlen aus `hls/lines_rom.h`,
erzeugt von `hls/gen_lines.py`; auf dem Monitor geprüft) oder bildgenau von einer Anwendung
(`vec_demo`, siehe unten).

- Der Core dividiert nicht. Wer die Linien liefert, rechnet aus den Endpunkten die Steigung;
  der Core addiert sie nur noch Zeile für Zeile auf.
- Ein Eintrag der Tabelle sind 2 Worte zu 64 Bit (16 Byte):

  | Feld | Bits | Inhalt |
  |------|------|--------|
  | `x`     | Wort 0, 15:0  | Spalte, an der die Linie in Zeile `y0` beginnt; läuft die Linie nach links, die rechte Kante des Startpixels (Spalte + 1) |
  | `y0`    | Wort 0, 31:16 | erste Zeile |
  | `y1`    | Wort 0, 47:32 | letzte Zeile (≥ `y0`) |
  | `flags` | Wort 0, 63:48 | Bit 0 gültig, Bit 1 flache Linie (\|dx\| > dy) |
  | `slope` | Wort 1, 31:0  | Schritt in x pro Zeile mit Vorzeichen, 16 Nachkommabits: ±(\|dx\| + 1) / (`y1` − `y0` + 1), zur Null hin abgerundet |

- In Zeile `y0 + k` belegt die Linie die Spalten von `x + k·slope` bis `x + (k+1)·slope`.
  Die Abschnitte aller 32 Linien rechnet der Core in den ersten Takten der Zeile davor,
  im Bild vergleicht er nur noch die Spalte mit 32 Abschnitten.
- Gezeichnet wird in Pixelpaaren, die Linien sind also 2 Pixel breit: bei 4:2:2 teilen sich zwei
  Pixel die Farbe, ein einzelnes grünes Pixel wäre je nach Spalte grün oder grau.
  Flache Linien werden zwei Zeilen hoch gezeichnet (Bit 1), damit sie nicht dünner wirken als steile.
- Die Pause am Bildanfang (Tabelle laden, Abschnitte der ersten Zeile) muss kurz bleiben, derzeit
  rund 100 Takte: `iq_scope` hält die letzten Pixel eines Bildes fest, bis wieder Pixel kommen, und die
  Videoausgabe hat nur 1024 Pixel FIFO. Eine Pause von einer ganzen Zeile (1920 Takte) reichte, damit
  der Monitor „no signal" meldete, obwohl der ADV7511 1080p60 erkannte.
- Anderes festes Bild: `picture()` in `hls/gen_lines.py` ändern, Skript starten, HLS und Hardware neu bauen.

#### Linien von einer Anwendung

```
vec_demo ──AXI-Lite (Tabelle, lines_ctrl)──► pattern_gen ──► wie oben
    ▲                                            │ frame_tick ──► frame_irq.v: Puls am Bildanfang
    └── read auf /dev/uioN ◄── uio_pdrv_genirq ◄─┴── IRQ_F2P[1] (SPI 62, steigende Flanke)
```

- Die Tabelle liegt im Core (AXI-Lite-Speicher ab `0x400`): zwei Puffer zu je 32 Einträgen,
  Puffer 1 ab `0x600`. Jedes 64-Bit-Wort liegt als zwei 32-Bit-Worte im Speicher, das untere zuerst.
- `lines_ctrl` (`0x48`): Bit 0 = Linien aus der Tabelle statt aus dem festen Bild, Bit 1 = Puffer 1.
  Der Core liest das Register am Anfang jedes Bildes und den gewählten Puffer in den ersten
  64 Takten danach; ein Schreibzugriff auf `lines_ctrl` schaltet also bildgenau um.
- Rückmeldung: `frames` (`0x60`) zählt die Bilder, `lines_cur` (`0x50`) zeigt die Einstellung, die
  für das laufende Bild übernommen wurde. Bei der Übernahme wechselt der Ausgang `frame_tick` seinen
  Pegel; `rtl/frame_irq.v` macht daraus einen Puls von 64 Takten (Interrupt). Das Bild wird danach noch fertig ausgegeben, sichtbar wird die Übernahme mit dem
  nächsten Bild, etwa 0,7 ms später (Rest des Bildes und Austastlücke).
- `vec_demo` wartet auf den Interrupt, rechnet die Linien für das nächste Bild (Steigung und
  Beschneiden am Bildrand in `target/lines.h`), schreibt sie in den Puffer, der gerade nicht gewählt
  ist, und wählt ihn. Dafür bleiben etwa 16 ms; gemessen braucht `vec_demo` dafür im Mittel 86 µs
  (höchstens 92 µs), bei 600 Bildern kein Bild zu spät und kein Interrupt verloren. Kommt die Wahl zu spät, zeigt der Core das vorige
  Bild noch einmal vollständig; `vec_demo` zählt das als „zu spät".
- Die Bewegung hängt am Bildzähler des Cores: jedes Bild zeigt den Stand seiner Nummer.
- Auf dem Monitor geprüft: der Würfel dreht sich flüssig, ohne Zerreißen.
- `set_pattern.sh 8` schaltet auf das feste Bild zurück (`lines_ctrl = 0`).
- Der Device-Tree-Knoten `pattern_gen` ist dafür ein UIO-Gerät mit Interrupt (`system-user.dtsi`);
  `set_pattern.sh` greift weiter über `/dev/mem` zu.

Auf dem Board (als root):

```bash
vec_demo                   # Würfel, Welle und Quadrat bewegen sich, bis Strg-C
vec_demo -n 600 -r 80      # 600 Bilder mit Echtzeit-Priorität
vec_demo -w 20             # Test: 20 ms Rechenzeit pro Bild, also jedes Bild zu spät
set_pattern.sh 8           # zurück zum festen Bild
```

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

## I/Q-Oszilloskop

`iq_scope` blendet in die Bildmitte ein Fenster von 1024 × 1024 Pixeln ein, das I nach rechts und Q nach oben
aufträgt. Quelle ist das eingebaute Testsignal oder der I/Q-Strom aus dem Ringpuffer (siehe unten).
Timing bei 148,5 MHz eingehalten.

```bash
hdmi_init.sh                         # nach jedem Start einmal
set_scope.sh kreis                   # Testsignal: Kreis
set_scope.sh lissajous               # Testsignal: Lissajous-Figur, die langsam wandert
set_scope.sh farbe=glut nachleuchten=5
set_scope.sh hintergrund=bild        # Testbild scheint durch, wo das Oszilloskop dunkel ist
set_scope.sh aus
set_scope.sh                         # Einstellung anzeigen
```

- **Bildspeicher:** 256 × 256 Zellen zu 16 Bit im Block-RAM (32 von 140 Block-RAMs, dazu einer für die Farbtabellen), kein Speicher im DDR. I und Q
  haben je 8 Bit, jeder Abtastwert trifft also genau eine Zelle und erhöht sie um `schritt`.
- **Anzeige:** jede Zelle ist 4 × 4 Pixel groß. Angezeigt wird der Zellwert, um `helligkeit` Bit nach rechts
  geschoben und auf 255 begrenzt, über eine von vier Farbtabellen (`gruen`, `glut`, `eis`, `grau`).
- **Nachleuchten:** einmal pro Bild wird jede Zelle um 1/2^n verkleinert (`nachleuchten=n`). Das erledigt die
  Speicherseite, die auch anzeigt; die andere Seite gehört den Abtastwerten.
- **Passende Werte:** viele Treffer pro Zelle (Träger, Testsignal) brauchen eine größere `helligkeit`-Zahl, wenige
  (Rauschen, kurze Signale) eine kleinere oder einen größeren `schritt`.
- Außerhalb des Fensters läuft das Bild von `pattern_gen` unverändert durch.

Simulation: `bash sim/run_sim.sh iq_scope` prüft Durchleitung, Kreis, einzelne Punkte und das Abklingen.

## I/Q-Daten in die PL

Linux schreibt die Abtastwerte in einen Ringpuffer im DDR (16 MiB, `reserved-memory/iqring@1f000000`), der
HLS-Core `iq_reader` liest sie über den HP1-Port und gibt sie als AXI4-Stream an das Oszilloskop.

```bash
set_scope.sh iq                      # Oszilloskop auf den I/Q-Eingang
iq_rx -p -f 100e6 -s 10e6 -t 30      # vom HackRF in die PL
iq_rx -z -s 16e6 -t 60               # Test ohne HackRF: Zählerdaten mit 16 Msps
iq_rx -z -s 0 -t 5                   # Zählerdaten ungebremst: was schafft die Strecke?
```

- **Ablauf:** `iq_rx` kopiert jeden Block (262144 Byte) über `/dev/mem` in den Ring und schreibt dann den
  Schreibzeiger ins Register `wr_ptr`. Der Core liest in Blöcken von 1024 Byte, sobald so viel bereitliegt, und
  meldet seinen Lesezeiger in `rd_ptr`. Ist der Ring voll, verwirft `iq_rx -p` den Block und zählt einen Überlauf.
- **Was „lückenlos" heißt:** kein Abtastwert fehlt. Die Werte kommen aber stoßweise in der PL an (Strom mit
  `tvalid`), nicht im gleichmäßigen Abtasttakt.
- **Prüfung in der PL:** der Core zählt die ausgegebenen Abtastwerte (`samples`) und die Sprünge, wenn man die
  Daten als 16-Bit-Zähler liest (`seq_errors`). `iq_rx -z` schreibt solche Zählerdaten und vergleicht am Ende.
- **Gemessen** (Zählerdaten, Oszilloskop als Abnehmer): 16 Msps über 5 s und 20 Msps über 60 s ohne Sprung und
  ohne fehlenden Wert, CPU-Last etwa 17 % eines Kerns. Ungebremst gehen 22,9 Msps durch; die Grenze setzt das
  Oszilloskop, das 6 Takte pro Abtastwert braucht (148,5 MHz / 6 = 24,75 Msps), nicht das Schreiben in den Ring.
- **Mit dem HackRF gemessen** (`iq_rx -p`): 10 Msps über 20 s und 16 Msps über 60 s lückenlos – keine Lücke
  laut HackRF, kein Ring-Überlauf, und der Core hat genau so viele Abtastwerte gezählt, wie geschrieben wurden
  (960 102 400 bei 16 Msps). CPU-Last 19 % bei 10 Msps, 30 % bei 16 Msps; der Ring war nie über 1 % gefüllt.
- Für ein Bild auf dem Oszilloskop einen starken Sender in die Mitte nehmen und die Verstärkung so wählen, dass
  der Pegel bei etwa −10 dBFS liegt, z. B. `iq_rx -p -f 93.7e6 -s 2e6 -l 32 -g 28 -t 600`.
- Zählerdaten ergeben auf dem Oszilloskop ein gleichmäßig gefülltes Fenster, weil alle Zellen gleich oft
  getroffen werden.

Simulation: `HLS_ONLY=iq_reader vitis-run --mode hls --tcl cosim_hls.tcl` (C-Simulation und RTL-Co-Simulation).

## Spektrum

Der Core `iq_spectrum` rechnet aus den I/Q-Daten laufend ein Spektrum, `spec_trace` zeichnet es als Kurve in das
Fenster des Oszilloskops: Frequenz nach rechts, Pegel nach oben.

```bash
iq_rx -p -f 100e6 -s 10e6 -t 600 &   # Daten in die PL
set_scope.sh spektrum                # Spektrum statt X/Y-Bild
set_scope.sh mittelung=6             # über 2^6 = 64 Blöcke mitteln (0..10)
set_scope.sh maxhold=1               # Max-Hold-Kurve dazu; maxhold=neu beginnt sie neu, maxhold=0 aus
set_scope.sh iq                      # zurück zum X/Y-Bild
```

- **Rechnung:** je 1024 Abtastwerte mit Hann-Fenster, FFT (Xilinx-FFT-IP über `hls::fft`), Leistung je Linie.
  Die Blöcke folgen lückenlos aufeinander, ohne Überlappung.
- **Blockmittelung:** die Leistungen von 2^n Blöcken werden je Linie aufsummiert und gemittelt (Mittelung der
  Leistung, nicht der dB-Werte). Erst danach geht ein Spektrum zur Anzeige; bei 10 Msps und `mittelung=6` sind das
  etwa 150 Spektren pro Sekunde, bei `mittelung=10` etwa 10.
- **Max-Hold:** je Linie der größte gemittelte Pegel seit dem letzten Löschen, als zweite Kurve gezeichnet.
- **Achsen:** das Fenster ist so breit wie die Abtastrate, die Mittenfrequenz liegt auf dem senkrechten Strich.
  Je Spalte sind 4 der 1024 Linien zu einem senkrechten Strich zusammengefasst (kleinster bis größter Pegel).
  Die ganze Höhe sind 96 dB (0,376 dB pro Zelle), der obere Rand ist Vollaussteuerung
  (Träger mit Amplitude 128), der waagerechte Strich liegt bei −48 dBFS. Leere Linien stehen bei Zelle 31 von 255.
- **Anzeige:** `spec_trace` zeichnet die Kurve etwa 71 mal pro Sekunde neu, unabhängig davon, wie oft ein neues
  Spektrum kommt; jede Zelle der Kurve wird dabei einmal getroffen. `set_scope.sh spektrum` stellt dazu
  `nachleuchten=2 schritt=32 helligkeit=0` ein. Längeres Nachleuchten lässt alte Kurven stehen.
- Ohne laufendes `iq_rx -p` kommen keine Daten, die Kurve bleibt dann auf dem letzten Stand stehen.

Simulation: `HLS_ONLY=iq_spectrum vitis-run --mode hls --tcl build_hls.tcl` (C-Simulation mit Trägern und
Rauschen), `bash sim/run_sim.sh spec_trace` (Kurve).

Auf dem Board gemessen: Zählerdaten (`iq_rx -z`) ergaben aus 60 030 976 Abtastwerten bei `mittelung=4` genau
3664 Spektren, also wird jeder Block verrechnet. Mit dem HackRF bei 10 Msps (`-f 98e6`, UKW) etwa 630 Spektren
pro Sekunde, 0 FFT-Überläufe, Datenweg lückenlos, CPU 19 %.

## Interrupt-Laufzeit

Wie lange braucht Linux, um auf einen Interrupt aus der PL zu antworten? Das Modul `irq_timer` setzt den IRQ und
merkt sich den Stand seines Zeitzählers (Start). Das Programm `irq_test` wird von Linux geweckt und schreibt über
AXI-Lite eine Quittung; in dem Takt, in dem dieser Schreibzugriff ankommt, hält das Modul die Zeit an (Stopp).

```
irq_timer ──IRQ_F2P[0]──► GIC ──► Kernel (uio_pdrv_genirq) ──► irq_test (read auf /dev/uioN)
    ▲                                                              │
    └──────────── AXI-Lite: ACK in STATUS = Stopp ◄────────────────┘
```

```bash
irq_test                       # 10000 Messungen, Pause 1 ms plus bis zu 0,66 ms Zufall
irq_test -n 100000 -r 80       # mit Echtzeit-Priorität (SCHED_FIFO 80)
irq_test -c 1 -H               # an Kern 1 gebunden, mit Verteilung der Laufzeiten
irq_test -i 100 -j 0 -n 0      # alle 100 µs ohne Zufall, bis Strg-C
irq_test -b                    # Vergleich ohne Interrupt: Statusregister in einer Schleife abfragen
irq_test -d -n 0               # Mittelwert und größte Laufzeit laufend auf der OLED-Anzeige (siehe unten)
```

- **Zeitbasis:** ein frei laufender 32-Bit-Zähler in der PL an FCLK0, ein Schritt sind 10 ns. Start und Stopp
  sind Stände dieses einen Zählers; die Laufzeit ist genau die Zahl der Takte, in denen der IRQ gesetzt war.
  Das Modul hängt ohne Taktübergang am 100-MHz-Bus.
- **Statistik in der PL:** Anzahl, Summe, Quadratsumme (96 Bit), kleinste und größte Laufzeit. `irq_test` rechnet
  daraus den Mittelwert, den Jitter als Standardabweichung (√(Quadratsumme/n − Mittelwert²)) und nennt die
  größte Laufzeit als schlechtesten Fall. Es geht also keine Messung verloren, auch wenn das Programm nicht mitkommt.
- **Median und Verteilung** zählt `irq_test` aus den einzelnen Laufzeiten (Register `LAST`) in Fächern von 1 µs.
  Das Lesen liegt nach dem Stopp, verlängert die Messung also nicht.
- **Nummer des schlechtesten Falls:** `irq_test` nennt zur größten Laufzeit die Nummer ihrer Messung (die erste
  hat die Nummer 1), auch auf der OLED-Anzeige (`worst 97us #1247`).
- **Vorlauf:** vor der Messung läuft ein Durchgang, der nicht zählt. Ohne ihn war die erste Messung regelmäßig
  erhöht (76 bis 109 µs statt etwa 42 µs) und stand in ruhigen Läufen als schlechtester Fall da (in 3 von 21
  Läufen). Die großen Ausreißer verteilen sich über den ganzen Lauf.
- **Pause:** nach jedem Stopp wartet das Modul `INTERVAL` Takte plus eine Zufallszahl (`RAND_MASK`), damit die
  Messung nicht im Gleichschritt mit dem Zeittakt von Linux (100 Hz) läuft. `-j` wird auf 2^k − 1 Takte abgerundet.
- **Was gemessen wird:** die ganze Strecke Interrupt-Controller → Kernel → Wechsel in das Programm → Schreibzugriff
  über den GP0-Port. `irq_test -b` zeigt zum Vergleich die Strecke ohne Interrupt (Lesen und Schreiben über AXI-Lite).
- **Treiber:** `system-user.dtsi` gibt dem Knoten `irq_timer@40050000` den Namen `generic-uio`; der Treiber
  `uio_pdrv_genirq` übernimmt ihn nur mit dem Parameter `of_id=generic-uio` (`/etc/modprobe.d/uio.conf` im Image).
  udev lädt das Modul beim Start von selbst; auf einer Karte ohne diese Datei lädt `irq_test` es neu. Der Treiber sperrt den IRQ bei jedem Auftreten,
  `irq_test` gibt ihn nach der Quittung wieder frei.
- Kernel: 6.12 mit `CONFIG_PREEMPT`, kein PREEMPT_RT. Ausreißer im Millisekundenbereich sind damit zu erwarten.

| Register | Offset | Inhalt |
|----------|--------|--------|
| `CTRL` | `0x00` | Bit 0 `enable`; Bit 1 `clear` (nur schreiben): Statistik löschen |
| `STATUS` | `0x04` | lesen: Bit 0 IRQ gesetzt, Bit 1 `LIMIT` erreicht, Bit 2 Laufzeit über 2^32 − 1 Takte (42,9 s); schreiben: Bit 0 = 1 quittiert (Stopp) |
| `INTERVAL`, `RAND_MASK` | `0x08`, `0x0C` | Pause in Takten und Maske für den zufälligen Zuschlag |
| `LIMIT` | `0x10` | Zahl der Messungen (0 = ohne Ende) |
| `COUNT`, `LAST`, `MIN`, `MAX` | `0x14` bis `0x20` | Anzahl, letzte, kleinste und größte Laufzeit in Takten |
| `SUM` | `0x24`, `0x28` | Summe der Laufzeiten, 64 Bit |
| `SQ` | `0x2C`, `0x30`, `0x34` | Summe der Quadrate, 96 Bit |
| `NOW`, `CLK_HZ` | `0x38`, `0x3C` | Zeitzähler und Takte pro Sekunde |

Auf dem Board gemessen (je 50 000 Messungen, `-i 300 -j 300`, Board sonst ohne Last):

| Aufruf | Mittelwert | Jitter (σ) | kleinste | größte | 99,9 % unter |
|--------|-----------:|-----------:|---------:|-------:|-------------:|
| `irq_test` (normale Priorität) | 42,9 µs | 3,0 µs | 38,8 µs | 152 µs | 73 µs |
| `irq_test -r 80` | 36,9 µs | 2,9 µs | 29,3 µs | 106 µs | 65 µs |
| `irq_test -r 80 -c 1` | 35,6 µs | 2,7 µs | 27,5 µs | 77 µs | 62 µs |
| `irq_test -b` (ohne Interrupt) | 0,42 µs | 5,4 µs | 0,22 µs | 434 µs | 11 µs |

Der Kernel hat dabei genau so viele Interrupts gezählt wie der Core Messungen (`grep irq_timer /proc/interrupts`).
In einem Lauf mit 5000 Messungen bei normaler Priorität lag ein einzelner Ausreißer bei 2,24 ms. Unter Last
(HackRF-Empfang, SD-Karte) ist noch nicht gemessen.

Simulation: `bash sim/run_sim.sh irq_timer` prüft die Laufzeit taktgenau gegen die Dauer des IRQ, dazu Statistik,
`LIMIT`, Pause, Löschen und Abschalten.

## OLED-Anzeige

Die Anzeige des ZedBoards (128 × 32 Pixel, Controller SSD1306) zeigt zwei Zeilen Text zu je 16 Zeichen.

```
oled / irq_test ──/dev/mem──► AXI Quad SPI (oled_spi) ──SCLK, SDIN──► SSD1306
                └────────────► AXI-GPIO (oled_ctrl) ──D/C, RES, VBAT, VDD──┘   (dazwischen oled_mux)
```

```bash
oled "ZedBoard" "Zeile 2"      # Text anzeigen; schaltet die Anzeige bei Bedarf ein
oled -u "ZedBoard"             # Bild um 180 Grad gedreht
oled aus                       # Anzeige abschalten
irq_test -d                    # Messung der Interrupt-Laufzeit: "mean" und "worst" mit Nummer der Messung, einmal pro Sekunde
irq_test -u                    # wie -d, Bild um 180 Grad gedreht
```

- **Ansteuerung ohne Kernel-Treiber:** die Programme schreiben die Register der beiden Cores über `/dev/mem`
  (`target/oled.h`). Der SPI-Knoten ist in `system-user.dtsi` abgeschaltet, damit der Treiber `spi-xilinx` ihn
  nicht belegt. Im Kernel fehlen `spidev` und ein Treiber für den SSD1306; der Weg über den Kernel bräuchte
  eine neue Kernel-Konfiguration und eine neu geschriebene Karte, die Hardware bliebe dieselbe.
- **Kein Bildspeicher in der PL:** das Bild liegt im Controller der Anzeige. Ein Text sind 1024 Byte über SPI
  (knapp 3 ms) und bleibt stehen, auch wenn das Programm endet.
- **Einschalten:** Folge und Befehle wie im ZedBoard-Beispiel von Digilent: alles aus, Logikversorgung (VDD),
  `AE`, Reset, Ladungspumpe (`8D 14`, `D9 F1`), Versorgung der Anzeige (VBAT), 200 ms warten, `81 0F`, `A0`, `C0`,
  `DA 20`, `AF`. Jeder Programmstart durchläuft sie ganz, die Anzeige ist dabei etwa 1 s dunkel. Das Aus- und
  Einschalten bringt auch den Bitzähler der Anzeige wieder in Tritt; sie hat keinen Chip Select.
- **64 statt 32 Zeilen:** der Controller bleibt auf seiner Vorgabe von 64 Zeilen, das Bild wird deshalb in beide
  Hälften seines Speichers geschrieben. Mit dem sonst üblichen Befehl für 32 Zeilen (`A8 1F`) blieb die Anzeige
  auf dem Board dunkel (in zwei Versuchsreihen; vermutlich liegen die 32 Zeilen des Glases mit `DA 20` auf der
  anderen Hälfte der Zeilenleitungen).
- **SPI-Takt:** 3,125 MHz wie im Beispiel von Digilent, Modus 0. Ob auch 6,25 MHz gingen, ist offen: der erste
  Bitstream lief damit, aber immer zusammen mit `A8 1F`.
- **Fehlersuche ohne SPI-Core:** Bit 4 des Steuerworts schaltet Takt und Daten auf die Bits 5 und 6 um
  (`rtl/oled_mux.v`); so lässt sich die Anzeige mit `devmem` Bit für Bit ansteuern.
- **D/C** darf erst wechseln, wenn die Bytes davor auf der Leitung waren: der SPI-Core liefert zu jedem
  gesendeten Byte eines zurück, das Programm wartet darauf.
- **Zeichensatz:** Terminus fett, 8 × 16 Pixel, Zeichen `0x20` bis `0x7E`; neu erzeugen mit
  `target/gen_oled_font.py > target/oled_font.h`.
- **`irq_test -d`:** für die Ausgabe hält das Programm die Messung kurz an (`enable = 0`), damit kein Interrupt
  in die Ausgabe fällt und zu lang gemessen wird.
- **Stand:** auf dem Board geprüft: Text mit `oled` lesbar und richtig herum; im SPI-Core gehen dabei alle Bytes
  ohne Moduskonflikt raus. Die Laufzeiten von `irq_test` mit und ohne `-d` unterscheiden sich nicht.
- Ein stehendes Bild brennt sich bei OLED mit der Zeit ein: nicht tagelang denselben Text zeigen, `oled aus`.

## HackRF an USB

Das HackRF hängt an der USB-OTG-Buchse J13 des ZedBoards, Linux empfängt die I/Q-Daten über `libhackrf`.
Getestet mit einem HackRF One r9 mit PortaPack (Mayhem-Firmware v2.0.1) im HackRF-Modus, JP2 und JP3 gesteckt,
ohne Hub.

Messreihe (`./hackrf_messreihe.sh 30`, je 30 Sekunden, Daten verworfen):

| Abtastrate | Datenrate | CPU-Last | Lücken | Ergebnis |
|-----------:|----------:|---------:|-------:|----------|
| 2 Msps     | 4 MB/s    | 3 %      | 0      | lückenlos |
| 4 Msps     | 8 MB/s    | 6 %      | 0      | lückenlos |
| 8 Msps     | 16 MB/s   | 13 %     | 0      | lückenlos |
| 10 Msps    | 20 MB/s   | 16 %     | 0      | lückenlos |
| 12,5 Msps  | 25 MB/s   | 20 %     | 0      | lückenlos |
| 16 Msps    | 32 MB/s   | 25 %     | 0      | lückenlos |
| 18 Msps    | 36 MB/s   | 28 %     | 11     | Lücken nur in der ersten Sekunde, danach keine mehr |
| 20 Msps    | 36,8 statt 40 MB/s | 29 % | rund 4500 pro Sekunde | geht nicht |

Die Grenze liegt bei etwa 36,9 MB/s. Die CPU-Last (100 % = ein Kern) ist dabei gering, der Engpass ist also die
USB-Strecke des Zynq und nicht der Prozessor. Sicher nutzbar sind Abtastraten bis 16 Msps.

**Anschluss**

- J13 ist eine Micro-USB-Buchse: Micro-USB-OTG-Adapter (Micro-A-Stecker auf USB-A-Buchse), daran das Kabel zum HackRF.
- JP2 stecken (5 V auf VBUS) und JP3 stecken (großer VBUS-Kondensator für Host-Betrieb). So steht es in der
  Jumper-Tabelle des ZedBoard Hardware User's Guide; am Board noch nicht geprüft. Ab Werk sind beide offen.
- Der USB-Controller läuft im Image bereits als Host (`dr_mode = "host"`, 480 Mbit/s), am Kernel ist nichts zu ändern.
- VBUS liefert nur etwa 500 mA. Meldet sich das HackRF nicht oder fällt es beim Empfang aus: Hub mit eigener Versorgung.
- PortaPack (Mayhem): im Hauptmenü „HackRF" wählen, dann verhält sich das Gerät am USB wie ein HackRF One.
  `hackrf_info` zeigt dabei die Versionsnummer der Mayhem-Firmware. Zurück mit der Reset-Taste.

**Prüfen und empfangen**

```bash
./board.sh run hackrf_info                    # Gerät, Seriennummer, Firmware
./board.sh run iq_rx                          # 100 MHz, 10 Msps, 10 Sekunden, Daten verwerfen
./board.sh run iq_rx -f 433.92e6 -s 8e6 -t 60
./board.sh run iq_rx -l 24 -g 30 -a           # LNA 0–40 dB, VGA 0–62 dB, -a = HF-Vorverstärker ein
./board.sh run iq_rx -t 2 -o /tmp/daten.iq    # in eine Datei schreiben ("-" = Standardausgabe)
./hackrf_messreihe.sh                         # 2, 4, 8, 10, 12.5, 16 und 20 Msps nacheinander, Tabelle
```

- Datenformat: 8 Bit mit Vorzeichen, abwechselnd I und Q, also 2 Byte pro Abtastwert. 10 Msps sind 20 MB/s,
  die höchste Rate von 20 Msps sind 40 MB/s.
- `iq_rx` gibt jede Sekunde Datenrate, Pegel (0 dBFS = Vollaussteuerung), CPU-Last (100 % = ein Kern) und die Zahl
  der Lücken aus. Die Lücken zählt das HackRF selbst: sein Puffer war voll, weil der Rechner die Daten nicht schnell
  genug abgeholt hat. Am Ende steht `ERGEBNIS: lueckenlos` (Rückgabe 0) oder `NICHT lueckenlos` (Rückgabe 1).
- Kennt die Firmware den Lückenzähler nicht, urteilt `iq_rx` nur nach der Datenrate und sagt das dazu.
- Spitze bei 0 dBFS heißt Übersteuerung: Verstärkung verringern.
- `/tmp` liegt im RAM: 2 Sekunden bei 10 Msps sind 40 MB. Das Schreiben auf die SD-Karte ist langsamer als der
  Datenstrom und erzeugt Lücken.

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
| `iq_rx`: „kein HackRF gefunden"           | `./board.sh run 'dmesg \| tail'` nach dem Einstecken: erscheint dort kein neues USB-Gerät, fehlt VBUS (JP2) oder der OTG-Adapter passt nicht. PortaPack im HackRF-Modus? |
| `hackrf_info`: „No HackRF boards found", in `dmesg` steht `ci_hdrc ... unable to init phy: -110` | Der USB-Baustein des Boards hat nach einem Neustart per `reboot` nicht geantwortet (einmal beobachtet, Ursache offen). Board aus- und einschalten |
| `iq_rx -p`/`-z`: „Knoten iq_reader@... nicht im Device Tree" | Auf dem Board läuft noch ein Bitstream ohne `iq_reader`: `./board.sh deploy` |
| `iq_rx -p`: Ring-Überläufe                   | Die PL nimmt die Daten nicht schnell genug ab; `iq_rx -z -s 0` zeigt, was die Strecke schafft |
| `hackrf_info`: „No HackRF boards found" nach einem Neustart des Boards | Das PortaPack ist dabei in seine eigene Oberfläche zurückgefallen: wieder „HackRF" wählen |
| `iq_rx`: Lücken schon bei kleiner Abtastrate | Schreibt `-o` auf die SD-Karte? Läuft daneben etwas Rechenintensives? Sonst Versorgung prüfen (Hub mit Netzteil) |
| `set_scope.sh`: „AXI-GPIO des Oszilloskops nicht im Device Tree" | Auf dem Board läuft noch ein Bitstream ohne `iq_scope`: `./board.sh deploy` |
| Oszilloskop: Fenster bleibt schwarz        | `helligkeit` zu groß oder `schritt` zu klein; mit `set_scope.sh kreis helligkeit=0` muss ein weißer Kreis erscheinen |
| Oszilloskop: alles weiß oder Fläche statt Linie | `helligkeit` vergrößern oder `nachleuchten` verkleinern |
| `irq_test`/`devmem`: „Bus error", in `dmesg` steht `external abort` | Device Tree mit `irq_timer`, aber Bitstream ohne: `BOOT.BIN` enthielt einen alten Bitstream. `build_petalinux.sh` nimmt ihn deshalb direkt aus der XSA |
| `irq_test`: „Knoten irq_timer@... nicht im Device Tree" | Auf dem Board läuft noch ein Bitstream ohne `irq_timer`: `./board.sh deploy` |
| `irq_test`: „kein UIO-Geraet"              | `rmmod uio_pdrv_genirq; modprobe uio_pdrv_genirq of_id=generic-uio` von Hand, dann `ls /sys/class/uio`. Das Modul liegt im Rootfs unter `/lib/modules`; fehlt es, die Karte neu schreiben. `cat /proc/device-tree/*/irq_timer@*/compatible` muss `generic-uio` zeigen |
| `irq_test`: „kein Interrupt angekommen"    | `grep irq_timer /proc/interrupts`: steht der Zähler, kommt der IRQ nicht im PS an (`IRQ_F2P` in `create_hw.tcl`) |
| `oled`: „Cores der OLED-Anzeige nicht im Device Tree" | Auf dem Board läuft noch ein Bitstream ohne OLED: `./board.sh deploy` |
| `oled`: „der SPI-Core sendet nicht"        | `devmem 0x44A00064` (Statusregister): Bit 4 gesetzt heißt Moduskonflikt (`ss_i` nicht auf 1). Belegt der Kernel-Treiber den Core? `status` des Knotens muss `disabled` sein |
| OLED bleibt dunkel, `oled` meldet keinen Fehler | `devmem 0x40060000` muss `0x2` oder `0x3` zeigen (beide Versorgungen ein). Wurde die Befehlsfolge in `target/oled.h` geändert? `A8 1F` (32 Zeilen) macht die Anzeige auf diesem Board dunkel |
| OLED: Text steht auf dem Kopf oder ist gespiegelt | `oled -u` bzw. `irq_test -u` |
| `codec_init.sh`: „Codec antwortet nicht"  | Bitstream geladen (ohne MCLK reagiert der Codec nicht)? `i2cdetect -y <bus>` muss `38` zeigen |

## Hinweis zu PetaLinux-Versionen

Ab 2024.1 nutzt PetaLinux die neue Kommandosyntax (`petalinux-create project`, `petalinux-package boot`).
Die Skripte probieren zuerst die neue und fallen dann auf die alte Syntax zurück.
PetaLinux 2025.2 meldet selbst, dass das Toolset mit 2026.2 abgekündigt wird; Nachfolger ist AMD EDF
mit Yocto-basiertem Workflow.
