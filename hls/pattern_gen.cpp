#include "pattern_gen.h"

#define BOX_SIZE 96

struct rgb_t { ap_uint<8> r, g, b; };

// RGB (voller Bereich) -> YCbCr nach BT.709, begrenzter Bereich (Y 16..235, C 16..240)
// Koeffizienten mit 2^12 skaliert
static void rgb_to_ycbcr(rgb_t p, ap_uint<8> &y, ap_uint<8> &cb, ap_uint<8> &cr) {
    ap_int<22> r = p.r, g = p.g, b = p.b;
    ap_int<22> ys  = (  748 * r + 2516 * g +  254 * b + 2048) >> 12;
    ap_int<22> cbs = ( -412 * r - 1387 * g + 1799 * b + 2048) >> 12;
    ap_int<22> crs = ( 1799 * r - 1634 * g -  165 * b + 2048) >> 12;
    y  = ys + 16;
    cb = cbs + 128;
    cr = crs + 128;
}

// Dreieckschwingung: 9 Bit Phase -> 0..255..0, ohne Sprung beim Ueberlauf
static ap_uint<8> triangle(ap_uint<9> phase) {
    ap_uint<8> v = phase(7, 0);
    return phase[8] ? ap_uint<8>(~v) : v;
}

void pattern_gen(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                 hls::stream<video_t> &m_axis_video) {
#pragma HLS INTERFACE s_axilite port=pattern bundle=ctrl
#pragma HLS INTERFACE s_axilite port=color   bundle=ctrl
#pragma HLS INTERFACE s_axilite port=format  bundle=ctrl
#pragma HLS INTERFACE axis port=m_axis_video
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS PIPELINE II=1

    // Ein Aufruf = ein Pixel
    static ap_uint<11> x = 0;
    static ap_uint<11> y = 0;

    // Register werden nur am Bildanfang uebernommen -> kein Wechsel mitten im Bild
    static ap_uint<8>  pattern_r = 0;
    static ap_uint<24> color_r   = 0;
    static ap_uint<2>  format_r  = 0;

    // Bewegtes Quadrat: Position und Richtung, ein Schritt pro Bild
    static ap_uint<11> box_x = 100;
    static ap_uint<11> box_y = 100;
    static bool box_right = true, box_down = true;

    static ap_uint<9> frame = 0;           // Bildzaehler fuer den Farbverlauf

    static ap_uint<8> cr_hold = 128;      // Cr des geraden Pixels fuer das ungerade

    bool sof = (x == 0 && y == 0);
    bool eol = (x == FRAME_WIDTH - 1);

    if (sof) {
        pattern_r = pattern;
        color_r   = color;
        format_r  = format;
    }

    // --- Testbild in RGB -----------------------------------------------------
    rgb_t p = {0, 0, 0};
    switch (pattern_r) {
    case PATTERN_BARS: {
        // 8 gleich breite Balken: weiss, gelb, cyan, gruen, magenta, rot, blau, schwarz
        ap_uint<3> bar = ~ap_uint<3>(x / (FRAME_WIDTH / 8));
        p.g = bar[2] ? 255 : 0;
        p.r = bar[1] ? 255 : 0;
        p.b = bar[0] ? 255 : 0;
        break;
    }
    case PATTERN_RAMP: {
        ap_uint<8> v = (x * 34) >> 8;     // 0..1919 -> 0..254
        p.r = v; p.g = v; p.b = v;
        break;
    }
    case PATTERN_CHECKER: {
        ap_uint<8> v = (x[6] ^ y[6]) ? 255 : 0;
        p.r = v; p.g = v; p.b = v;
        break;
    }
    case PATTERN_SOLID:
        p.r = color_r(23, 16); p.g = color_r(15, 8); p.b = color_r(7, 0);
        break;
    case PATTERN_BOX: {
        bool inside = (x >= box_x && x < box_x + BOX_SIZE &&
                       y >= box_y && y < box_y + BOX_SIZE);
        if (inside) { p.r = 255; p.g = 255; p.b = 255; }
        else        { p.r = 0;   p.g = 0;   p.b = 96;  }
        break;
    }
    case PATTERN_GRADIENT: {
        // Rot laeuft horizontal, Gruen vertikal, Blau diagonal; jede Farbe wandert
        // mit eigener Geschwindigkeit (ein Durchlauf des Bildzaehlers = 512 Bilder)
        ap_uint<9> t = frame;
        p.r = triangle(ap_uint<9>(x >> 2) + t);
        p.g = triangle(ap_uint<9>(y >> 2) + (t << 1));
        p.b = triangle(ap_uint<9>((x + y) >> 3) - t);
        break;
    }
    default:                               // PATTERN_BLACK und unbekannte Werte
        break;
    }

    // --- RGB -> YCbCr 4:2:2 ----------------------------------------------------
    ap_uint<8> yy, cb, cr;
    rgb_to_ycbcr(p, yy, cb, cr);

    bool odd = x[0];
    if (!odd) cr_hold = cr;
    ap_uint<8> c;
    if (format_r & FORMAT_SWAP_CBCR) c = odd ? cb : cr;       // (ungefiltert, nur Fehlersuche)
    else                             c = odd ? cr_hold : cb;

    video_t out;
    out.data = (format_r & FORMAT_SWAP_YC) ? (yy, c) : (c, yy);
    out.user = sof;
    out.last = eol;
    out.keep = -1;
    out.strb = -1;
    out.id   = 0;
    out.dest = 0;
    m_axis_video.write(out);

    // --- Zaehler ---------------------------------------------------------------
    if (eol) {
        x = 0;
        if (y == FRAME_HEIGHT - 1) {
            y = 0;
            frame++;
            // Quadrat fuer das naechste Bild verschieben, an den Raendern umkehren
            if (box_right) { if (box_x >= FRAME_WIDTH - BOX_SIZE - 4) box_right = false; else box_x += 4; }
            else           { if (box_x < 4)                          box_right = true;  else box_x -= 4; }
            if (box_down)  { if (box_y >= FRAME_HEIGHT - BOX_SIZE - 3) box_down = false; else box_y += 3; }
            else           { if (box_y < 3)                           box_down = true;  else box_y -= 3; }
        } else {
            y++;
        }
    } else {
        x++;
    }
}
