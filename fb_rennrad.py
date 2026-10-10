#!/usr/bin/env python3
# =============================================================================
#  Testbild "Rennrad" auf dem PC zeichnen und in einen Framebuffer des Boards laden
#
#  Aufruf: ./fb_rennrad.py            Bild zeichnen und in Puffer 1 laden
#          ./fb_rennrad.py 2          in Puffer 2 (0..2) laden
#          ./fb_rennrad.py -o bild.ppm    nur die Datei schreiben (auch .png)
#
#  Weg: Python (Pillow) zeichnet 1920x1080 in RGB -> PPM -> board.sh kopiert die Datei
#  nach /tmp auf dem Board -> fb_test wandelt nach YCbCr 4:2:2 und schreibt den Puffer.
#  Prueft damit auch das Laden eigener Bilder ueber fb_test N datei.ppm.
# =============================================================================
import math
import os
import subprocess
import sys
import tempfile

from PIL import Image, ImageDraw, ImageFilter

W, H = 1920, 1080
S = 2                       # doppelt so gross zeichnen, dann verkleinern -> glatte Kanten
DX = -45                    # Rad waagerecht in die Bildmitte ruecken

RED, RED_HI = (200, 16, 24), (255, 96, 88)
BLACK, RUBBER, TAN = (14, 14, 16), (28, 28, 30), (176, 132, 84)
STEEL, SILVER, WHITE = (120, 124, 130), (196, 200, 206), (240, 240, 236)


def pt(p):
    return ((p[0] + DX) * S, p[1] * S)


def tube(d, a, b, width, color):
    """Rohr mit runden Enden."""
    d.line([pt(a), pt(b)], fill=color, width=int(width * S))
    for p in (a, b):
        disc(d, p, width / 2, color)


def disc(d, c, r, color):
    x, y = pt(c)
    d.ellipse([x - r * S, y - r * S, x + r * S, y + r * S], fill=color)


def ring(d, c, r, width, color):
    x, y = pt(c)
    d.ellipse([x - r * S, y - r * S, x + r * S, y + r * S], outline=color, width=int(width * S))


def curve(d, points, width, color):
    """Glatter Linienzug durch die Punkte (Catmull-Rom)."""
    p = [points[0]] + list(points) + [points[-1]]
    out = []
    for i in range(1, len(p) - 2):
        for k in range(12):
            t = k / 12
            out.append(tuple(
                0.5 * (2 * p[i][j] + (p[i + 1][j] - p[i - 1][j]) * t
                       + (2 * p[i - 1][j] - 5 * p[i][j] + 4 * p[i + 1][j] - p[i + 2][j]) * t * t
                       + (3 * p[i][j] - p[i - 1][j] - 3 * p[i + 1][j] + p[i + 2][j]) * t ** 3)
                for j in range(2)))
    out.append(points[-1])
    for a, b in zip(out, out[1:]):
        tube(d, a, b, width, color)


def background():
    img = Image.new("RGB", (W * S, H * S))
    d = ImageDraw.Draw(img)
    horizon = 760
    top, mid, low = (18, 20, 60), (190, 70, 60), (255, 176, 80)     # Abendhimmel
    for y in range(horizon * S):
        t = y / (horizon * S)
        a, b, u = (top, mid, t / 0.6) if t < 0.6 else (mid, low, (t - 0.6) / 0.4)
        d.line([(0, y), (W * S, y)], fill=tuple(int(a[i] + (b[i] - a[i]) * u) for i in range(3)))
    disc(d, (1500 - DX, 640), 120, (255, 226, 150))                 # Sonne
    for base, height, color, seed in ((horizon, 250, (70, 36, 70), 3), (horizon, 140, (38, 22, 46), 7)):
        pts = [(0, base)]                                           # zwei Bergketten
        for x in range(0, W + 40, 40):
            y = base - height * (0.45 + 0.3 * math.sin(x / 190 + seed) + 0.25 * math.sin(x / 67 + 2 * seed))
            pts.append((x, y))
        pts.append((W, base))
        d.polygon([(x * S, y * S) for x, y in pts], fill=color)
    d.rectangle([0, horizon * S, W * S, H * S], fill=(30, 30, 34))  # Strasse
    for y in range(horizon, H):
        v = 26 + (y - horizon) // 14
        d.line([(0, y * S), (W * S, y * S)], fill=(v, v, v + 3), width=S)
    d.rectangle([0, 1022 * S, W * S, 1030 * S], fill=(220, 220, 210))    # Randlinie
    for x in range(40, W, 260):                                          # Mittellinie hinten
        d.rectangle([x * S, 800 * S, (x + 130) * S, 805 * S], fill=(200, 200, 190))
    return img


def wheel(d, c, back):
    r = 300
    ring(d, c, r, 22, RUBBER)                       # Reifen
    ring(d, c, r - 20, 5, TAN)                      # helle Flanke
    ring(d, c, r - 24, 34, BLACK)                   # hohe Carbonfelge
    ring(d, c, r - 56, 2, STEEL)
    n = 24
    for i in range(n):                              # Speichen, abwechselnd links/rechts am Flansch
        a = 2 * math.pi * (i + 0.5) / n
        h = a + (0.5 if i % 2 else -0.5)
        tube(d, (c[0] + 16 * math.cos(h), c[1] + 16 * math.sin(h)),
             (c[0] + (r - 57) * math.cos(a), c[1] + (r - 57) * math.sin(a)), 1.5, SILVER)
    disc(d, c, 18 if back else 13, BLACK)
    disc(d, c, 5, SILVER)


def bike(d):
    rear, front, bb = (560, 700), (1450, 700), (924, 763)
    seat_top, seat_low = (786, 297), (794, 325)     # Sattelrohr oben, Ansatz der Sitzstreben
    head_top, head_low = (1272, 253), (1311, 382)

    d.ellipse([pt((300, 985))[0], pt((0, 985))[1], pt((1720, 0))[0], pt((0, 1015))[1]], fill=(14, 14, 16))  # Schatten

    # hintere Kurbel und Pedal (liegen hinter dem Rahmen)
    tube(d, bb, (bb[0] - 148, bb[1] - 44), 14, (60, 60, 64))
    tube(d, (bb[0] - 172, bb[1] - 46), (bb[0] - 124, bb[1] - 42), 12, (40, 40, 44))

    wheel(d, rear, True)
    wheel(d, front, False)

    # Rahmen
    tube(d, rear, bb, 16, RED)                       # Kettenstrebe
    tube(d, rear, seat_low, 13, RED)                 # Sitzstrebe
    tube(d, bb, seat_top, 30, RED)                   # Sattelrohr
    tube(d, bb, (1304, 366), 40, RED)                # Unterrohr
    tube(d, (792, 318), (1276, 272), 30, RED)        # Oberrohr
    tube(d, head_top, head_low, 40, RED)             # Steuerrohr
    tube(d, head_low, (1352, 470), 26, BLACK)        # Gabel, zum Ausfallende schlanker
    tube(d, (1352, 470), front, 16, BLACK)
    for a, b in (((940, 742), (1290, 372)), ((800, 310), (1268, 266))):      # Glanzlicht
        tube(d, a, b, 4, RED_HI)
    tube(d, (1010, 668), (1210, 458), 9, WHITE)      # Schriftband auf dem Unterrohr

    # Sattelstuetze und Sattel
    tube(d, seat_top, (756, 195), 20, BLACK)
    d.polygon([pt(p) for p in ((640, 172), (700, 166), (800, 172), (878, 184), (880, 192),
                               (800, 196), (700, 198), (646, 190))], fill=BLACK)

    # Vorbau, Lenker mit Bremsgriff
    tube(d, head_top, (1264, 226), 30, BLACK)
    tube(d, (1264, 226), (1366, 216), 22, BLACK)
    curve(d, [(1366, 216), (1440, 214), (1486, 236), (1498, 286), (1466, 334), (1404, 346), (1336, 346)], 16, (40, 40, 44))
    d.polygon([pt(p) for p in ((1436, 204), (1470, 200), (1500, 150), (1516, 156), (1500, 226), (1450, 230))], fill=BLACK)
    curve(d, [(1508, 162), (1504, 230), (1478, 296)], 7, SILVER)

    # Trinkflasche am Unterrohr
    tube(d, (1030, 610), (1120, 512), 44, WHITE)
    tube(d, (1126, 506), (1136, 495), 26, BLACK)
    tube(d, (1048, 590), (1096, 538), 40, (60, 110, 200))

    # Antrieb: Kette, Kassette, Schaltwerk, Kettenblatt, Kurbel, Pedal
    chain = (150, 150, 154)
    tube(d, (rear[0] + 4, rear[1] - 44), (bb[0] + 6, bb[1] - 96), 5, chain)
    tube(d, (592, 822), (bb[0] - 4, bb[1] + 96), 5, chain)
    tube(d, (rear[0] - 6, rear[1] + 44), (578, 766), 5, chain)
    for r, color in ((46, STEEL), (36, SILVER), (26, STEEL)):
        ring(d, rear, r, 5, color)
    tube(d, (rear[0] + 6, rear[1] + 10), (580, 766), 12, BLACK)      # Schaltwerk
    tube(d, (580, 766), (592, 808), 10, BLACK)
    for c in ((580, 766), (592, 808)):
        disc(d, c, 14, (60, 60, 64))
        disc(d, c, 4, SILVER)
    ring(d, bb, 96, 9, BLACK)                        # grosses Kettenblatt
    ring(d, bb, 72, 5, (60, 60, 64))                 # kleines dahinter
    for i in range(5):                               # Kurbelstern
        a = 2 * math.pi * i / 5 + 0.3
        tube(d, bb, (bb[0] + 88 * math.cos(a), bb[1] + 88 * math.sin(a)), 12, BLACK)
    tube(d, bb, (bb[0] + 148, bb[1] + 44), 18, SILVER)               # vordere Kurbel
    disc(d, bb, 20, BLACK)
    disc(d, bb, 7, SILVER)
    tube(d, (bb[0] + 122, bb[1] + 46), (bb[0] + 176, bb[1] + 42), 14, BLACK)


def draw():
    img = background()
    bike(ImageDraw.Draw(img))
    img = img.resize((W, H), Image.LANCZOS)
    return img.filter(ImageFilter.UnsharpMask(radius=1, percent=40))


def main():
    args = sys.argv[1:]
    here = os.path.dirname(os.path.abspath(__file__))
    if len(args) == 2 and args[0] == "-o":
        draw().save(args[1])
        print("geschrieben:", args[1])
        return
    if len(args) > 1 or (args and args[0] not in ("0", "1", "2")):
        sys.exit(__doc__ or "Aufruf: fb_rennrad.py [0|1|2] | -o DATEI")
    n = args[0] if args else "1"
    board = os.path.join(here, "board.sh")
    with tempfile.NamedTemporaryFile(suffix=".ppm") as f:
        draw().save(f.name)
        with open(f.name, "rb") as data:
            subprocess.run([board, "run", "cat > /tmp/rennrad.ppm"], stdin=data, check=True)
    subprocess.run([board, "run", f"fb_test {n} /tmp/rennrad.ppm && rm /tmp/rennrad.ppm"], check=True)


if __name__ == "__main__":
    main()
