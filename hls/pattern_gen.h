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
    PATTERN_COUNT    = 8
};

// Bits des Registers "format" (nur zur Fehlersuche an der HDMI-Strecke)
#define FORMAT_SWAP_CBCR 0x1    // Cr auf geraden, Cb auf ungeraden Pixeln
#define FORMAT_SWAP_YC   0x2    // Y und Cb/Cr im Datenwort vertauscht

typedef ap_axiu<16, 1, 1, 1> video_t;

// fb_addr0..2: Byte-Adressen der Framebuffer (durch 8 teilbar)
// fb_frames:   so viele Bilder lang wird jeder Framebuffer gezeigt (0 wie 1)
void pattern_gen(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                 ap_uint<32> fb_addr0, ap_uint<32> fb_addr1, ap_uint<32> fb_addr2,
                 ap_uint<16> fb_frames,
                 ap_uint<64> *fb,
                 hls::stream<video_t> &m_axis_video);

#endif
