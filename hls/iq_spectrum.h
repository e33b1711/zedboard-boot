// Spektrum der I/Q-Daten: FFT mit 1024 Punkten, Blockmittelung der Leistung, Max-Hold
//   Eingang:  tdata[7:0] = I, tdata[15:8] = Q (wie am Ausgang von iq_reader)
//   Ablauf:   je 1024 Abtastwerte mit Hann-Fenster -> FFT -> Leistung je Frequenzlinie.
//             Die Leistungen von 2^avg_log2 Bloecken werden gemittelt; danach geht ein
//             Spektrum mit 1024 Werten hinaus (tlast am letzten Wert).
//   Ausgang:  tdata[7:0] = gemittelter Pegel, tdata[15:8] = Max-Hold davon
//             Reihenfolge wie die FFT: Linie 0 = Mittenfrequenz, 1..511 darueber,
//             512..1023 darunter (-fs/2 .. -fs/1024)
//   Pegel:    255 = Vollaussteuerung (Traeger mit Amplitude 128), eine Stufe = 10*log10(2)/8
//             = 0,376 dB, 0 liegt also 96 dB darunter
#ifndef IQ_SPECTRUM_H
#define IQ_SPECTRUM_H

#include <ap_axi_sdata.h>
#include <ap_fixed.h>
#include <ap_int.h>
#include <hls_stream.h>

#define FFT_BITS 10
#define FFT_LEN  (1 << FFT_BITS)
#define AVG_MAX  10                       // hoechstens 2^10 = 1024 Bloecke mitteln

#define LEVEL_FULL_SCALE 255
#define LEVEL_PER_OCTAVE 8                // Stufen je Verdopplung der Leistung (3,01 dB)

typedef ap_axiu<16, 0, 0, 0> spectrum_t;

// avg_log2:  Mittelung ueber 2^avg_log2 Bloecke (0 = jede FFT einzeln, hoechstens AVG_MAX)
// maxhold:   1 = Max-Hold laeuft; 0 = Max-Hold folgt dem gemittelten Pegel (loeschen)
// spectra:   ausgegebene Spektren seit dem Start
// overflows: FFT-Bloecke mit Ueberlauf in der Rechnung (sollte 0 bleiben)
void iq_spectrum(ap_uint<4> avg_log2, ap_uint<1> maxhold,
                 ap_uint<32> *spectra, ap_uint<32> *overflows,
                 hls::stream<ap_uint<16> > &s_axis_iq,
                 hls::stream<spectrum_t> &m_axis_spec);

#endif
