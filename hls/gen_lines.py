#!/usr/bin/env python3
# =============================================================================
#  Festes Linienbild fuer Testbild 8 erzeugen: schreibt hls/lines_rom.h
#
#  Aufruf: hls/gen_lines.py
#
#  32 Linien fuer 1920x1080: Rahmen, Raute und ein Stern mit 24 Strahlen (alle 15 Grad,
#  also auch waagerecht, senkrecht und diagonal, in beide Richtungen).
#
#  Der Core dividiert nicht: aus den Endpunkten wird hier die Steigung gerechnet.
#  Format eines Eintrags (2 Worte zu 64 Bit, siehe hls/pattern_gen.h):
#    x      Spalte, an der die Linie in Zeile y0 beginnt (bei einer Linie nach links
#           die rechte Kante des Startpixels, also Spalte + 1)
#    y0,y1  erste und letzte Zeile
#    slope  Schritt in x pro Zeile, Festkomma mit 16 Nachkommabits, mit Vorzeichen;
#           zur Null hin abgerundet, damit die Linie nie ueber ihren Endpunkt hinauslaeuft
#    flags  Bit 0 gueltig, Bit 1 flache Linie (wird zwei Zeilen hoch gezeichnet)
# =============================================================================
import math
import os

WIDTH, HEIGHT = 1920, 1080
LINE_COUNT = 32
FRAC = 16
FLAG_VALID, FLAG_FLAT = 1, 2


def encode(xa, ya, xb, yb):
    """Endpunkte (einschliesslich) -> (x, y0, y1, flags, slope)."""
    if ya > yb:
        xa, ya, xb, yb = xb, yb, xa, ya
    rows = yb - ya + 1
    dx = xb - xa
    step = ((abs(dx) + 1) << FRAC) // rows        # die Linie ueberstreicht |dx| + 1 Pixel
    flags = FLAG_VALID | (FLAG_FLAT if abs(dx) > yb - ya else 0)
    if dx >= 0:
        return xa, ya, yb, flags, step
    return xa + 1, ya, yb, flags, -step


def picture():
    cx, cy = WIDTH // 2, HEIGHT // 2
    l, r, t, b = 40, WIDTH - 41, 40, HEIGHT - 41
    lines = [(l, t, r, t), (r, t, r, b), (r, b, l, b), (l, b, l, t)]            # Rahmen
    lines += [(cx, t, r, cy), (r, cy, cx, b), (cx, b, l, cy), (l, cy, cx, t)]   # Raute
    for i in range(24):                                                         # Stern
        a = math.radians(15 * i)
        c, s = math.cos(a), math.sin(a)
        lines.append((round(cx + 60 * c), round(cy + 60 * s), round(cx + 440 * c), round(cy + 440 * s)))
    assert len(lines) == LINE_COUNT
    return lines


def main():
    lines = picture()
    out = [
        "// Festes Linienbild fuer Testbild 8 - erzeugt von hls/gen_lines.py, nicht von Hand aendern",
        "#ifndef LINES_ROM_H",
        "#define LINES_ROM_H",
        "",
        "// je Linie zwei Worte: {flags, y1, y0, x} und {0, slope}",
        "static const ap_uint<64> LINES_ROM[2 * LINE_COUNT] = {",
    ]
    for p in lines:
        x, y0, y1, flags, slope = encode(*p)
        w0 = x | y0 << 16 | y1 << 32 | flags << 48
        w1 = slope & 0xFFFFFFFF
        out.append("    0x%016XULL, 0x%016XULL,   // (%d,%d)-(%d,%d)" % ((w0, w1) + p))
    out += [
        "};",
        "",
        "#ifndef __SYNTHESIS__",
        "// Endpunkte der Linien, nur fuer die Testbench",
        "static const int LINES_POINTS[LINE_COUNT][4] = {",
    ]
    out += ["    {%d, %d, %d, %d}," % p for p in lines]
    out += ["};", "#endif", "", "#endif", ""]

    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "lines_rom.h")
    with open(path, "w") as f:
        f.write("\n".join(out))
    print("%s geschrieben (%d Linien)" % (path, len(lines)))


if __name__ == "__main__":
    main()
