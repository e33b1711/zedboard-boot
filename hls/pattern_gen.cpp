#include "pattern_gen.h"
#include "lines_rom.h"

#define BOX_SIZE 96

#define LINE_STEPS 4                           // Takte fuer die Abschnitte einer Zeile
typedef ap_uint<11 + LINE_FRAC> line_x_t;      // Spalte mit Nachkommabits
typedef ap_int<12 + LINE_FRAC>  line_s_t;      // Schritt pro Zeile

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

// Einstellungen eines Bildes; der Leser uebernimmt die Register und gibt sie weiter,
// damit Leser und Ausgabe fuer jedes Bild dasselbe sehen
struct cfg_t {
    ap_uint<8>  pattern;
    ap_uint<24> color;
    ap_uint<2>  format;
};

// Ein Aufruf = ein Bild: Register uebernehmen und, falls gewaehlt, einen Framebuffer
// in Bursts aus dem Speicher in das FIFO lesen
static void fb_reader(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                      ap_uint<32> fb_addr0, ap_uint<32> fb_addr1, ap_uint<32> fb_addr2,
                      ap_uint<16> fb_frames, ap_uint<64> *fb,
                      hls::stream<cfg_t> &cfg_s, hls::stream<ap_uint<64> > &fb_s) {
    static ap_uint<2>  index = 0;          // aktueller Framebuffer
    static ap_uint<16> shown = 0;          // so oft wurde er schon gezeigt

    cfg_t cfg;
    cfg.pattern = pattern;
    cfg.color   = color;
    cfg.format  = format;
    cfg_s.write(cfg);

    if (pattern == PATTERN_FB) {
        ap_uint<32> addr = (index == 0) ? fb_addr0 : (index == 1) ? fb_addr1 : fb_addr2;
        ap_uint<29> base = addr >> 3;      // Wortadresse
        for (int i = 0; i < FRAME_WORDS; i++) {
#pragma HLS PIPELINE II=1
            fb_s.write(fb[base + i]);
        }
        if (shown + 1 >= fb_frames) {
            shown = 0;
            index = (index == FB_COUNT - 1) ? ap_uint<2>(0) : ap_uint<2>(index + 1);
        } else {
            shown++;
        }
    }
}

// Ein Aufruf = ein Bild: Testbild rechnen oder Framebuffer-Daten durchreichen
static void draw(hls::stream<cfg_t> &cfg_s, hls::stream<ap_uint<64> > &fb_s,
                 const ap_uint<64> lines[2 * LINE_BUF_WORDS], ap_uint<2> lines_ctrl,
                 ap_uint<2> &lines_cur, ap_uint<32> &frames, ap_uint<1> &frame_tick,
                 hls::stream<video_t> &m_axis_video) {
    // Bewegtes Quadrat: Position und Richtung, ein Schritt pro Bild
    static ap_uint<11> box_x = 100;
    static ap_uint<11> box_y = 100;
    static bool box_right = true, box_down = true;

    static ap_uint<9> frame = 0;           // Bildzaehler fuer den Farbverlauf
    static ap_uint<32> frame_count = 0;    // Bildzaehler fuer die Anwendung

    // Einstellungen gelten fuer das ganze Bild -> kein Wechsel mitten im Bild
    cfg_t cfg = cfg_s.read();
    ap_uint<8>  pattern_r = cfg.pattern;
    ap_uint<24> color_r   = cfg.color;
    ap_uint<2>  format_r  = cfg.format;
    bool from_fb = (pattern_r == PATTERN_FB);

    ap_uint<8>  cr_hold = 128;             // Cr des geraden Pixels fuer das ungerade
    ap_uint<64> word = 0;                  // 4 Pixel aus dem Framebuffer

    // --- Linien: alles in Registern, die Abschnitte aller Linien werden parallel verglichen ---
    bool        ln_valid[LINE_COUNT], ln_flat[LINE_COUNT];
    ap_uint<11> ln_y0[LINE_COUNT], ln_y1[LINE_COUNT];
    line_x_t    ln_x[LINE_COUNT];          // Spalte am oberen Rand der naechsten Zeile
    line_s_t    ln_slope[LINE_COUNT];
    // Zwischenwerte der Rechnung fuer die naechste Zeile (ein Schritt pro Takt)
    bool        st_act[LINE_COUNT];
    line_x_t    st_b[LINE_COUNT];          // Spalte am unteren Rand der naechsten Zeile
    ap_uint<10> st_l[LINE_COUNT], st_r[LINE_COUNT];
    // Abschnitt [l, r) in Pixelpaaren: der vorigen Zeile allein (pv), der naechsten (nx)
    // und der laufenden Zeile (cu)
    bool        pv_on[LINE_COUNT], nx_on[LINE_COUNT], cu_on[LINE_COUNT];
    ap_uint<10> pv_l[LINE_COUNT], pv_r[LINE_COUNT], nx_l[LINE_COUNT], nx_r[LINE_COUNT];
    ap_uint<10> cu_l[LINE_COUNT], cu_r[LINE_COUNT];
#pragma HLS ARRAY_PARTITION variable=ln_valid complete
#pragma HLS ARRAY_PARTITION variable=ln_flat  complete
#pragma HLS ARRAY_PARTITION variable=ln_y0    complete
#pragma HLS ARRAY_PARTITION variable=ln_y1    complete
#pragma HLS ARRAY_PARTITION variable=ln_x     complete
#pragma HLS ARRAY_PARTITION variable=ln_slope complete
#pragma HLS ARRAY_PARTITION variable=st_act   complete
#pragma HLS ARRAY_PARTITION variable=st_b     complete
#pragma HLS ARRAY_PARTITION variable=st_l     complete
#pragma HLS ARRAY_PARTITION variable=st_r     complete
#pragma HLS ARRAY_PARTITION variable=pv_on    complete
#pragma HLS ARRAY_PARTITION variable=pv_l     complete
#pragma HLS ARRAY_PARTITION variable=pv_r     complete
#pragma HLS ARRAY_PARTITION variable=nx_on    complete
#pragma HLS ARRAY_PARTITION variable=nx_l     complete
#pragma HLS ARRAY_PARTITION variable=nx_r     complete
#pragma HLS ARRAY_PARTITION variable=cu_on    complete
#pragma HLS ARRAY_PARTITION variable=cu_l     complete
#pragma HLS ARRAY_PARTITION variable=cu_r     complete

    // Linientabelle fuer dieses Bild laden. lines_ctrl wird hier gelesen, nicht am Anfang
    // des ganzen Aufrufs (der Leser laeuft dem Bild voraus): die Anwendung kann also bis kurz
    // vor dem Ende des sichtbaren Bildes umschalten. frame_tick wechselt hier seinen Pegel
    // (ein Puls 1-0 wuerde von HLS wegoptimiert, weil in C nur der letzte Wert zaehlt).
    ap_uint<2> ctrl = lines_ctrl;
    frame_count++;
    frames    = frame_count;
    lines_cur = ctrl;
    frame_tick = frame_count[0];
    bool from_table = ctrl & LINES_CTRL_TABLE;
    ap_uint<8> base = (ctrl & LINES_CTRL_BUF1) ? LINE_BUF_WORDS : 0;
    for (int j = 0; j < 2 * LINE_COUNT; j++) {
#pragma HLS PIPELINE II=1
        ap_uint<64> w = from_table ? lines[base + j] : LINES_ROM[j];
        int i = j >> 1;
        if ((j & 1) == 0) {
            ln_x[i]     = line_x_t(w(10, 0)) << LINE_FRAC;
            ln_y0[i]    = w(26, 16);
            ln_y1[i]    = w(42, 32);
            ln_valid[i] = w[48];
            ln_flat[i]  = w[49];
            st_act[i] = false; st_b[i] = 0; st_l[i] = 0; st_r[i] = 0;
            pv_on[i] = false; pv_l[i] = 0; pv_r[i] = 0;
            nx_on[i] = false; nx_l[i] = 0; nx_r[i] = 0;
            cu_on[i] = false; cu_l[i] = 0; cu_r[i] = 0;
        } else {
            ln_slope[i] = w(11 + LINE_FRAC, 0);
        }
    }
    // Durchlauf 0 gibt nichts aus und ist nur LINE_STEPS Takte lang: in ihm werden die
    // Abschnitte der Linien fuer Bildzeile 0 gerechnet. Durchlauf row gibt Bildzeile row - 1
    // aus und rechnet fuer Bildzeile row. Die Pause am Bildanfang muss kurz bleiben: iq_scope
    // haelt die letzten Pixel eines Bildes fest, bis hier wieder Pixel kommen, und die
    // Videoausgabe hat nur ein FIFO von 1024 Pixeln.
    ap_uint<11> row = 0, x = 0;
    for (int n = 0; n < FRAME_WIDTH * FRAME_HEIGHT + LINE_STEPS; n++) {
#pragma HLS PIPELINE II=1
        {
            bool visible = (row != 0);
            ap_uint<11> y = row - 1;
            bool sof = (x == 0 && y == 0);
            bool eol = (x == FRAME_WIDTH - 1);                  // letztes Pixel der Bildzeile
            bool end = visible ? eol : (x == LINE_STEPS - 1);   // letzter Takt des Durchlaufs

            // --- Linien: liegt das Pixelpaar in einem Abschnitt der laufenden Zeile? ---
            ap_uint<10> pair = x >> 1;
            bool line_hit = false;
            for (int i = 0; i < LINE_COUNT; i++) {
#pragma HLS UNROLL
                if (cu_on[i] && pair >= cu_l[i] && pair < cu_r[i]) line_hit = true;
            }

            // --- Linien: Abschnitte fuer die naechste Zeile, verteilt auf die ersten Takte ---
            if (x < 3 || end)
            for (int i = 0; i < LINE_COUNT; i++) {
#pragma HLS UNROLL
                if (x == 0) {
                    // die Linie laeuft in dieser Zeile von ln_x nach st_b
                    st_act[i] = ln_valid[i] && row >= ln_y0[i] && row <= ln_y1[i];
                    st_b[i]   = ln_x[i] + ln_slope[i];
                } else if (x == 1) {
                    // belegte Spalten [lo abgerundet, hi aufgerundet), auf Pixelpaare erweitert;
                    // welches Ende links liegt, sagt das Vorzeichen der Steigung
                    bool left = ln_slope[i][11 + LINE_FRAC];
                    line_x_t from = left ? st_b[i] : ln_x[i];
                    line_x_t to   = left ? ln_x[i] : st_b[i];
                    ap_uint<11> lo = from >> LINE_FRAC;
                    ap_uint<12> hi = ap_uint<12>(to >> LINE_FRAC) + (to(LINE_FRAC - 1, 0) != 0 ? 1 : 0);
                    st_l[i] = lo >> 1;
                    st_r[i] = (hi + 1) >> 1;
                    if (st_act[i]) ln_x[i] = st_b[i];
                } else if (x == 2) {
                    // flache Linien: der Abschnitt der vorigen Zeile wird noch einmal gezeichnet
                    bool prev = ln_flat[i] && pv_on[i];
                    nx_on[i] = st_act[i] || prev;
                    nx_l[i]  = (!prev || (st_act[i] && st_l[i] < pv_l[i])) ? st_l[i] : pv_l[i];
                    nx_r[i]  = (!prev || (st_act[i] && st_r[i] > pv_r[i])) ? st_r[i] : pv_r[i];
                    pv_on[i] = st_act[i];
                    pv_l[i]  = st_l[i];
                    pv_r[i]  = st_r[i];
                }
                if (end) {
                    cu_on[i] = nx_on[i];
                    cu_l[i]  = nx_l[i];
                    cu_r[i]  = nx_r[i];
                }
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
            case PATTERN_LINES:
                if (line_hit) p.g = 255;
                break;
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

            // --- Framebuffer: Pixel liegen schon als YCbCr 4:2:2 im Speicher -----------
            if (from_fb && visible) {
                ap_uint<2> k = x(1, 0);
                if (k == 0) word = fb_s.read();
                ap_uint<2> kc = (format_r & FORMAT_SWAP_CBCR) ? ap_uint<2>(k ^ 1) : k;
                ap_uint<16> px_y = word >> (16 * k);
                ap_uint<16> px_c = word >> (16 * kc);
                yy = px_y(7, 0);
                c  = px_c(15, 8);
            }

            video_t out;
            out.data = (format_r & FORMAT_SWAP_YC) ? (yy, c) : (c, yy);
            out.user = sof;
            out.last = eol;
            out.keep = -1;
            out.strb = -1;
            out.id   = 0;
            out.dest = 0;
            if (visible) m_axis_video.write(out);

            if (end) { x = 0; row++; } else { x++; }
        }
    }

    // Quadrat fuer das naechste Bild verschieben, an den Raendern umkehren
    frame++;
    if (box_right) { if (box_x >= FRAME_WIDTH - BOX_SIZE - 4) box_right = false; else box_x += 4; }
    else           { if (box_x < 4)                          box_right = true;  else box_x -= 4; }
    if (box_down)  { if (box_y >= FRAME_HEIGHT - BOX_SIZE - 3) box_down = false; else box_y += 3; }
    else           { if (box_y < 3)                           box_down = true;  else box_y -= 3; }
}

void pattern_gen(ap_uint<8> pattern, ap_uint<24> color, ap_uint<2> format,
                 ap_uint<32> fb_addr0, ap_uint<32> fb_addr1, ap_uint<32> fb_addr2,
                 ap_uint<16> fb_frames,
                 ap_uint<64> *fb,
                 const ap_uint<64> lines[2 * LINE_BUF_WORDS], ap_uint<2> lines_ctrl,
                 ap_uint<2> &lines_cur, ap_uint<32> &frames, ap_uint<1> &frame_tick,
                 hls::stream<video_t> &m_axis_video) {
#pragma HLS INTERFACE s_axilite port=pattern   bundle=ctrl
#pragma HLS INTERFACE s_axilite port=color     bundle=ctrl
#pragma HLS INTERFACE s_axilite port=format    bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fb_addr0  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fb_addr1  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fb_addr2  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=fb_frames bundle=ctrl
#pragma HLS INTERFACE s_axilite port=lines      bundle=ctrl
#pragma HLS INTERFACE s_axilite port=lines_ctrl bundle=ctrl
#pragma HLS INTERFACE s_axilite port=lines_cur  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=frames     bundle=ctrl
#pragma HLS INTERFACE ap_none   port=frame_tick
// lines_ctrl liest draw am Bildanfang direkt aus dem Register, ohne Abgleich mit dem Aufruf
#pragma HLS STABLE variable=lines_ctrl
// Bursts hoechstens 16 Worte lang: der HP-Port des PS spricht AXI3
#pragma HLS INTERFACE m_axi port=fb bundle=fb offset=off depth=FB_MEM_WORDS max_read_burst_length=16 num_read_outstanding=4
#pragma HLS INTERFACE axis port=m_axis_video
// Laeuft frei: ap_start liegt im Block Design fest auf 1
#pragma HLS INTERFACE ap_ctrl_hs port=return
#pragma HLS DATAFLOW

    hls::stream<cfg_t> cfg_s("cfg");
    hls::stream<ap_uint<64> > fb_s("fb");
#pragma HLS STREAM variable=cfg_s depth=2
#pragma HLS STREAM variable=fb_s  depth=512      // 2048 Pixel = gut eine Zeile Vorrat

    fb_reader(pattern, color, format, fb_addr0, fb_addr1, fb_addr2, fb_frames, fb, cfg_s, fb_s);
    draw(cfg_s, fb_s, lines, lines_ctrl, lines_cur, frames, frame_tick, m_axis_video);
}
