#!/usr/bin/env python3
# =============================================================================
#  Farbtabellen des I/Q-Oszilloskops erzeugen: schreibt rtl/iq_palette.v
#
#  Aufruf: rtl/gen_palette.py
#
#  4 Tabellen zu je 256 Helligkeitsstufen, Eintrag = {Y, Cb, Cr} (BT.709, begrenzter Bereich):
#    0 gruen   Leuchtschirm: gruen, bei grosser Helligkeit nach weiss
#    1 glut    schwarz, rot, gelb, weiss
#    2 eis     blau, tuerkis, weiss
#    3 grau
#  Kleine Werte werden angehoben (Gamma 0,6), damit einzelne Treffer sichtbar sind.
# =============================================================================
import os

GAMMA = 0.6


def ramp(t, stops):
    """Farbe zwischen den Stuetzstellen (Position 0..1, (r, g, b) 0..1)."""
    for (p0, c0), (p1, c1) in zip(stops, stops[1:]):
        if t <= p1:
            u = (t - p0) / (p1 - p0)
            return tuple(a + (b - a) * u for a, b in zip(c0, c1))
    return stops[-1][1]


PALETTES = [
    ("gruen", [(0, (0, 0, 0)), (0.6, (0.1, 1, 0.2)), (1, (1, 1, 1))]),
    ("glut",  [(0, (0, 0, 0)), (0.4, (1, 0, 0)), (0.75, (1, 0.9, 0)), (1, (1, 1, 1))]),
    ("eis",   [(0, (0, 0, 0)), (0.4, (0, 0.2, 1)), (0.75, (0, 0.9, 1)), (1, (1, 1, 1))]),
    ("grau",  [(0, (0, 0, 0)), (1, (1, 1, 1))]),
]


def ycbcr(r, g, b):
    y = 0.2126 * r + 0.7152 * g + 0.0722 * b
    clamp = lambda v: max(16, min(240, round(v)))
    return min(235, round(16 + 219 * y)), clamp(128 + 224 * (b - y) / 1.8556), clamp(128 + 224 * (r - y) / 1.5748)


def main():
    lines = []
    for index, (name, stops) in enumerate(PALETTES):
        lines.append(f"        // {index}: {name}")
        for level in range(256):
            y, cb, cr = ycbcr(*ramp((level / 255) ** GAMMA, stops))
            lines.append(f"        rom[{index * 256 + level}] = 24'h{y:02X}{cb:02X}{cr:02X};")
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "iq_palette.v")
    with open(path, "w") as f:
        f.write(f"""// Farbtabellen des I/Q-Oszilloskops, erzeugt von gen_palette.py – nicht von Hand aendern
// addr = {{Tabelle, Helligkeit}}, q = {{Y, Cb, Cr}}
`timescale 1ns / 1ps

module iq_palette (
    input  wire        clk,
    input  wire        ce,
    input  wire [9:0]  addr,
    output reg  [23:0] q
);

    (* rom_style = "block" *) reg [23:0] rom [0:1023];

    initial begin
{chr(10).join(lines)}
    end

    always @(posedge clk)
        if (ce) q <= rom[addr];

endmodule
""")
    print("geschrieben:", path)


if __name__ == "__main__":
    main()
