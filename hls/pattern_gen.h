// Testbild-Generator fuer Vitis HLS: 1280x720, YCbCr 4:2:2 (16 Bit) als AXI4-Stream-Video
//   tdata[7:0] = Y, tdata[15:8] = Cb (gerade Pixel) / Cr (ungerade Pixel)
//   tuser = Start of Frame, tlast = End of Line
#ifndef PATTERN_GEN_H
#define PATTERN_GEN_H

#include <ap_int.h>
#include <ap_axi_sdata.h>
#include <hls_stream.h>

#define FRAME_WIDTH  1280
#define FRAME_HEIGHT 720

// Werte des Registers "pattern"
enum {
    PATTERN_BLACK    = 0,
    PATTERN_BARS     = 1,   // 8 Farbbalken, 100 %
    PATTERN_RAMP     = 2,   // Graukeil horizontal
    PATTERN_CHECKER  = 3,   // Schachbrett, 64 Pixel
    PATTERN_SOLID    = 4,   // Vollflaeche in der Farbe aus Register "color"
    PATTERN_BOX      = 5,   // bewegtes Quadrat
    PATTERN_COUNT    = 6
};

// Bits des Registers "format" (nur zur Fehlersuche an der HDMI-Strecke)
#define FORMAT_SWAP_CBCR 0x1    // Cr auf geraden, Cb auf ungeraden Pixeln
#define FORMAT_SWAP_YC   0x2    // Y und Cb/Cr im Datenwort vertauscht

typedef ap_axiu<16, 1, 1, 1> video_t;

void pattern_gen(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                 hls::stream<video_t> &m_axis_video);

#endif
