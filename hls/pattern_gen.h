// Testbild-Generator fuer Vitis HLS: 1920x1080, YCbCr 4:2:2 (16 Bit) als AXI4-Stream-Video
//   tdata[7:0] = Y, tdata[15:8] = Cb (gerade Pixel) / Cr (ungerade Pixel)
//   tuser = Start of Frame, tlast = End of Line
// Zusaetzlich: Ausgabe aus drei Framebuffern im DDR ueber einen AXI-Master (nur lesend)
//   Speicherformat = Stream-Format: 2 Byte pro Pixel, Byte 0 = Y, Byte 1 = Cb/Cr ("YUYV"),
//   zeilenweise ohne Luecken, FRAME_BYTES pro Bild
#ifndef PATTERN_GEN_H
#define PATTERN_GEN_H

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

// Bildgroesse; nur fuer die RTL-Co-Simulation von aussen verkleinert (Breite durch 8 teilbar)
#ifndef FRAME_WIDTH
#define FRAME_WIDTH  1920
#define FRAME_HEIGHT 1080
#endif

#define FB_COUNT    3
#define FRAME_BYTES (FRAME_WIDTH * FRAME_HEIGHT * 2)
#define FRAME_WORDS (FRAME_BYTES / 8)              // 64-Bit-Worte, je 4 Pixel
#define FB_MEM_WORDS (FB_COUNT * FRAME_WORDS)      // Speichermodell der Simulation

// Werte des Registers "pattern"
enum {
    PATTERN_BLACK    = 0,
    PATTERN_BARS     = 1,   // 8 Farbbalken, 100 %
    PATTERN_RAMP     = 2,   // Graukeil horizontal
    PATTERN_CHECKER  = 3,   // Schachbrett, 64 Pixel
    PATTERN_SOLID    = 4,   // Vollflaeche in der Farbe aus Register "color"
    PATTERN_BOX      = 5,   // bewegtes Quadrat
    PATTERN_GRADIENT = 6,   // Farbverlauf, der sich mit der Zeit verschiebt
    PATTERN_FB       = 7,   // Framebuffer 0, 1, 2 im Wechsel
    PATTERN_LINES    = 8,   // 32 Linien, gruen auf schwarz
    PATTERN_COUNT    = 9
};

// Linien fuer PATTERN_LINES. Der Core dividiert nicht: wer die Linien liefert, rechnet die
// Steigung aus den Endpunkten (siehe hls/gen_lines.py). Ein Eintrag = 2 Worte zu 64 Bit:
//   Wort 0  [15:0]  x      Spalte, an der die Linie in Zeile y0 beginnt; laeuft die Linie
//                          nach links, die rechte Kante des Startpixels (Spalte + 1)
//           [31:16] y0     erste Zeile
//           [47:32] y1     letzte Zeile (>= y0)
//           [63:48] flags  LINE_VALID, LINE_FLAT
//   Wort 1  [31:0]  slope  Schritt in x pro Zeile, mit Vorzeichen, LINE_FRAC Nachkommabits:
//                          +-(|x1 - x0| + 1) / (y1 - y0 + 1), zur Null hin abgerundet
//           [63:32] frei (0)
// In Zeile y0 + k belegt die Linie die Spalten von x + k * slope bis x + (k + 1) * slope.
// Gezeichnet wird in Pixelpaaren (2 Pixel breit), weil sich bei 4:2:2 zwei Pixel die
// Farbe teilen.
#define LINE_COUNT 32
#define LINE_FRAC  16
#define LINE_VALID 0x1      // Eintrag wird gezeichnet
#define LINE_FLAT  0x2      // flache Linie (|dx| > dy): zwei Zeilen hoch zeichnen

// Linientabelle der Anwendung: zwei Puffer im Core (AXI-Lite-Speicher "lines"), je
// 2 * LINE_COUNT Worte zu 64 Bit; Puffer 1 beginnt bei Wort LINE_BUF_WORDS.
// Register "lines_ctrl" wird am Anfang jedes Bildes gelesen, ein einziger Schreibzugriff
// schaltet also bildgenau um. Zur Rueckmeldung zaehlt "frames" die Bilder, "lines_cur"
// zeigt die Einstellung, die fuer das laufende Bild uebernommen wurde, und der Ausgang
// frame_tick wechselt dabei seinen Pegel (rtl/frame_irq.v macht daraus den Interrupt).
#define LINE_BUF_WORDS   (2 * LINE_COUNT)
#define LINES_CTRL_TABLE 0x1    // Linien aus der Tabelle statt aus dem festen Bild
#define LINES_CTRL_BUF1  0x2    // Puffer 1 statt Puffer 0

// Bits des Registers "format" (nur zur Fehlersuche an der HDMI-Strecke)
#define FORMAT_SWAP_CBCR 0x1    // Cr auf geraden, Cb auf ungeraden Pixeln
#define FORMAT_SWAP_YC   0x2    // Y und Cb/Cr im Datenwort vertauscht

typedef ap_axiu<16, 1, 1, 1> video_t;

// fb_addr0..2: Byte-Adressen der Framebuffer (durch 8 teilbar)
// fb_frames:   so viele Bilder lang wird jeder Framebuffer gezeigt (0 wie 1)
// lines, lines_ctrl, lines_cur, frames, frame_tick: Linien aus der Anwendung, siehe oben
void pattern_gen(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                 ap_uint<32> fb_addr0, ap_uint<32> fb_addr1, ap_uint<32> fb_addr2,
                 ap_uint<16> fb_frames,
                 ap_uint<64> *fb,
                 const ap_uint<64> lines[2 * LINE_BUF_WORDS], ap_uint<2> lines_ctrl,
                 ap_uint<2> &lines_cur, ap_uint<32> &frames, ap_uint<1> &frame_tick,
                 hls::stream<video_t> &m_axis_video);

#endif
