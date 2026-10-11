#!/usr/bin/env python3
"""Zeichensatz fuer die OLED-Anzeige erzeugen (target/oled_font.h).

Liest eine Konsolenschrift im Format PSF mit 8 x 16 Pixeln (Vorgabe: Terminus fett aus
dem Paket console-setup) und schreibt die Zeichen 0x20 bis 0x7E als C-Tabelle: 16 Byte
je Zeichen, eine Zeile pro Byte, Bit 7 ist das linke Pixel.

Aufruf: target/gen_oled_font.py [schrift.psf.gz] > target/oled_font.h
"""
import gzip
import struct
import sys

path = sys.argv[1] if len(sys.argv) > 1 else "/usr/share/consolefonts/Lat15-TerminusBold16.psf.gz"
data = gzip.open(path, "rb").read()

# Kopf: PSF1 (4 Byte) oder PSF2 (32 Byte); danach die Zeichen, danach die Unicode-Tabelle
glyph_of = {}
if data[:2] == b"\x36\x04":
    mode, size = data[2], data[3]
    header, count, width, height, has_table = 4, 512 if mode & 1 else 256, 8, size, mode & 6
    # je Zeichen eine Folge von 16-Bit-Werten, Ende mit 0xFFFF (0xFFFE leitet Folgen ein)
    table = data[header + count * size:]
    values = struct.unpack(f"<{len(table) // 2}H", table[:len(table) // 2 * 2])
    glyph, single = 0, True
    for v in values:
        if v == 0xFFFF:
            glyph, single = glyph + 1, True
        elif v == 0xFFFE:
            single = False
        elif single:
            glyph_of.setdefault(v, glyph)
elif data[:4] == b"\x72\xb5\x4a\x86":
    version, header, flags, count, size, height, width = struct.unpack("<7I", data[4:32])
    has_table = flags & 1
    # je Zeichen eine Folge von UTF-8-Zeichen, Ende mit 0xFF (0xFE leitet Folgen ein)
    table = data[header + count * size:]
    for glyph, entry in enumerate(table.split(b"\xff")[:count]):
        for char in entry.split(b"\xfe")[0].decode("utf-8", "replace"):
            glyph_of.setdefault(ord(char), glyph)
else:
    sys.exit(f"{path}: keine PSF-Schrift")
if (width, height, size) != (8, 16, 16):
    sys.exit(f"{path}: Schrift hat nicht 8 x 16 Pixel")
if not has_table:
    sys.exit(f"{path}: Schrift ohne Unicode-Tabelle")

print("/* Erzeugt von target/gen_oled_font.py aus", path.split("/")[-1], "- nicht von Hand aendern.")
print(" * Zeichen 0x20 bis 0x7E, 8 x 16 Pixel: 16 Byte je Zeichen, eine Zeile pro Byte,")
print(" * Bit 7 ist das linke Pixel. */")
print("static const unsigned char oled_font[95][16] = {")
for code in range(0x20, 0x7F):
    start = header + glyph_of[code] * size
    rows = ",".join(f"0x{b:02x}" for b in data[start:start + size])
    name = "backslash" if code == 0x5C else chr(code)
    print(f"    {{{rows}}},  /* {name} */")
print("};")
