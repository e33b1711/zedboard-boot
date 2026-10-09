#include "sine_gen.h"
#include <math.h>

// Viertelwelle mit halbem Schritt Versatz -> exakt spiegelsymmetrisch
static void init_quarter_lut(ap_uint<SAMPLE_BITS - 1> lut[LUT_SIZE]) {
    for (int i = 0; i < LUT_SIZE; i++) {
        double v = AMPLITUDE * sin((i + 0.5) * M_PI / (2.0 * LUT_SIZE));
        lut[i] = (ap_uint<SAMPLE_BITS - 1>)(v + 0.5);    // runden (v >= 0)
    }
}

void sine_gen(ap_uint<32> phase_inc, hls::stream<sample_t> &m_axis) {
#pragma HLS INTERFACE s_axilite port=phase_inc bundle=ctrl
#pragma HLS INTERFACE axis port=m_axis
#pragma HLS INTERFACE ap_ctrl_none port=return
#pragma HLS PIPELINE II=1

    static ap_uint<32> phase = 0;
    ap_uint<SAMPLE_BITS - 1> lut[LUT_SIZE];
    init_quarter_lut(lut);

    // Phase -> Quadrant (2 Bit) + Tabellenindex
    ap_uint<2>        quadrant = phase(31, 30);
    ap_uint<LUT_BITS> index    = phase(29, 30 - LUT_BITS);
    if (quadrant[0]) index = ~index;                 // 2. und 4. Quadrant: gespiegelt

    sample_t value = lut[index];
    if (quadrant[1]) value = -value;                 // 3. und 4. Quadrant: negativ

    if (phase_inc == 0) {                            // Stille, Phase zuruecksetzen
        value = 0;
        phase = 0;
    } else {
        phase += phase_inc;
    }
    m_axis.write(value);
}
