// Sinus-Generator (DDS) fuer Vitis HLS
//   f_out = phase_inc * f_sample / 2^32      (phase_inc = 0 -> Stille)
#ifndef SINE_GEN_H
#define SINE_GEN_H

#include <ap_int.h>
#include <hls_stream.h>

#define SAMPLE_BITS 24
#define LUT_BITS    10                     // Viertelwellen-Tabelle: 2^10 Eintraege
#define LUT_SIZE    (1 << LUT_BITS)
#define AMPLITUDE   4194303.0              // 2^22 - 1  =  -6 dBFS

typedef ap_int<SAMPLE_BITS> sample_t;

void sine_gen(ap_uint<32> phase_inc, hls::stream<sample_t> &m_axis);

#endif
