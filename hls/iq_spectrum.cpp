#include "iq_spectrum.h"
#include <complex>
#include <hls_fft.h>
#include <math.h>

// FFT-IP von Xilinx: 1024 Punkte, 16 Bit Ein- und Ausgang, skaliert, als Pipeline
struct fft_params : hls::ip_fft::params_t {
    static const unsigned output_ordering = hls::ip_fft::natural_order;
};
typedef hls::ip_fft::config_t<fft_params> fft_config_t;
typedef hls::ip_fft::status_t<fft_params> fft_status_t;
typedef ap_fixed<16, 1> fft_data_t;
typedef std::complex<fft_data_t> fft_complex_t;

// Die FFT teilt in jeder ihrer 5 Doppelstufen durch 4, zusammen durch 1024. Ein Traeger mit
// voller Amplitude kommt (wegen des Fensters, Mittelwert 1/2) mit Betrag 1/2 heraus: kein
// Ueberlauf, und seine Leistung ist genau 2^28.
#define FFT_SCALING 0x2AA
#define FULL_SCALE_LOG2 28

static void init_hann(ap_ufixed<16, 0, AP_RND, AP_SAT> window[FFT_LEN]) {
    for (int i = 0; i < FFT_LEN; i++)
        window[i] = 0.5 - 0.5 * cos(2.0 * M_PI * i / FFT_LEN);
}

// Ein Aufruf = ein Block: 1024 Abtastwerte mit dem Hann-Fenster gewichten
static void window(hls::stream<ap_uint<16> > &s_axis_iq, fft_complex_t xn[FFT_LEN],
                   hls::stream<fft_config_t> &config_s) {
    ap_ufixed<16, 0, AP_RND, AP_SAT> hann[FFT_LEN];
    init_hann(hann);

    fft_config_t config;
    config.setDir(1);                      // vorwaerts
    config.setSch(FFT_SCALING);
    config_s.write(config);

    for (int i = 0; i < FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
        ap_uint<16> s = s_axis_iq.read();
        ap_fixed<8, 1> re, im;             // -1 .. +127/128
        re.range() = s(7, 0);
        im.range() = s(15, 8);
        xn[i] = fft_complex_t(fft_data_t(re * hann[i]), fft_data_t(im * hann[i]));
    }
}

// 8 * log2(v), gerundet; 0 fuer v = 0
static ap_uint<9> log2_times_8(ap_uint<44> v) {
    // 8 * log2(1 + f / 32) fuer die 5 Bit unter der fuehrenden Eins
    static const ap_uint<4> FRACTION[32] = {0, 0, 1, 1, 1, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4,
                                            5, 5, 5, 5, 6, 6, 6, 6, 6, 7, 7, 7, 7, 7, 8, 8};
    if (v == 0) return 0;
    ap_uint<6> zeros = v.countLeadingZeros();
    ap_uint<44> norm = v << zeros;         // fuehrende Eins auf Bit 43
    ap_uint<6> msb = 43 - zeros;
    return msb * 8 + FRACTION[norm(42, 38)];
}

// Ein Aufruf = ein Block: Leistung je Linie aufsummieren; beim letzten Block der Mittelung
// den Pegel ausrechnen, Max-Hold nachfuehren und das Spektrum ausgeben
static void detect(fft_complex_t xk[FFT_LEN], hls::stream<fft_status_t> &status_s,
                   ap_uint<4> avg_log2, ap_uint<1> maxhold,
                   ap_uint<32> *spectra, ap_uint<32> *overflows,
                   hls::stream<spectrum_t> &m_axis_spec) {
    static ap_uint<44> sum[FFT_LEN];       // 32 Bit Leistung + 10 Bit Mittelung, mit Reserve
    static ap_uint<8>  held[FFT_LEN];
    static ap_uint<11> block = 0;          // Bloecke in der laufenden Mittelung
    static ap_uint<32> spectra_count = 0, overflow_count = 0;

    ap_uint<4> shift = avg_log2 > AVG_MAX ? ap_uint<4>(AVG_MAX) : avg_log2;
    bool first = block == 0;
    bool last = block + 1 >= (ap_uint<11>(1) << shift);

    for (int i = 0; i < FFT_LEN; i++) {
#pragma HLS PIPELINE II=1
#pragma HLS DEPENDENCE variable=sum  inter false
#pragma HLS DEPENDENCE variable=held inter false
        fft_complex_t line = xk[i];        // xk ist ein Strom: jedes Element genau einmal lesen
        ap_int<16> re, im;
        re.range() = line.real().range();
        im.range() = line.imag().range();
        ap_uint<32> power = re * re + im * im;
        ap_uint<44> total = (first ? ap_uint<44>(0) : sum[i]) + power;
        sum[i] = total;

        if (last) {
            // Mittelwert = total / 2^shift, also 8 * shift Stufen weniger
            ap_int<11> level = ap_int<11>(log2_times_8(total)) - LEVEL_PER_OCTAVE * shift
                             + (LEVEL_FULL_SCALE - LEVEL_PER_OCTAVE * FULL_SCALE_LOG2);
            ap_uint<8> now = level < 0 ? ap_uint<8>(0) : level > 255 ? ap_uint<8>(255) : ap_uint<8>(level);
            ap_uint<8> peak = (maxhold && held[i] > now) ? held[i] : now;
            held[i] = peak;

            spectrum_t out;
            out.data = (peak, now);
            out.keep = -1;
            out.strb = -1;
            out.last = i == FFT_LEN - 1;
            m_axis_spec.write(out);
        }
    }

    if (status_s.read().getOvflo()) overflow_count++;
    if (last) {
        block = 0;
        spectra_count++;
    } else {
        block++;
    }
    *spectra = spectra_count;
    *overflows = overflow_count;
}

void iq_spectrum(ap_uint<4> avg_log2, ap_uint<1> maxhold,
                 ap_uint<32> *spectra, ap_uint<32> *overflows,
                 hls::stream<ap_uint<16> > &s_axis_iq,
                 hls::stream<spectrum_t> &m_axis_spec) {
#pragma HLS INTERFACE s_axilite port=avg_log2  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=maxhold   bundle=ctrl
#pragma HLS INTERFACE s_axilite port=spectra   bundle=ctrl
#pragma HLS INTERFACE s_axilite port=overflows bundle=ctrl
#pragma HLS INTERFACE axis port=s_axis_iq
#pragma HLS INTERFACE axis port=m_axis_spec
// Laeuft frei: ap_start liegt im Block Design fest auf 1
#pragma HLS INTERFACE ap_ctrl_hs port=return
#pragma HLS DATAFLOW

    fft_complex_t xn[FFT_LEN];
    fft_complex_t xk[FFT_LEN];
    hls::stream<fft_config_t> config_s("config");
    hls::stream<fft_status_t> status_s("status");
#pragma HLS STREAM variable=config_s depth=2
#pragma HLS STREAM variable=status_s depth=2

    window(s_axis_iq, xn, config_s);
    hls::fft<fft_params>(xn, xk, status_s, config_s);
    detect(xk, status_s, avg_log2, maxhold, spectra, overflows, m_axis_spec);
}
