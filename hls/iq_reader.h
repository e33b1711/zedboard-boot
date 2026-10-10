// Leser fuer den I/Q-Ringpuffer im DDR: Linux schreibt Abtastwerte hinein und meldet den
// Schreibzeiger, der Core liest sie ueber einen AXI-Master und gibt sie als Strom aus.
//   Speicherformat: 2 Byte pro Abtastwert, Byte 0 = I, Byte 1 = Q (so liefert es das HackRF)
//   Strom:          tdata[7:0] = I, tdata[15:8] = Q
// Gelesen wird in Bloecken von IQ_CHUNK_BYTES; ein angefangener Block wartet, bis er voll ist.
#ifndef IQ_READER_H
#define IQ_READER_H

#include <ap_int.h>
#include <hls_stream.h>

#define IQ_CHUNK_WORDS   128                     // 64-Bit-Worte pro Block, je 4 Abtastwerte
#define IQ_CHUNK_BYTES   (IQ_CHUNK_WORDS * 8)
#define IQ_CHUNK_SAMPLES (IQ_CHUNK_WORDS * 4)
#define IQ_MEM_WORDS     2048                    // Speichermodell der Simulation

// enable:     0 = anhalten, Lesezeiger auf den Schreibzeiger setzen, Zaehler loeschen
// ring_addr:  Byte-Adresse des Ringpuffers (durch IQ_CHUNK_BYTES teilbar)
// ring_bytes: Groesse in Byte, Zweierpotenz
// wr_ptr:     Schreibzeiger von Linux, Byte-Abstand vom Anfang des Rings
// rd_ptr:     Lesezeiger des Cores, Byte-Abstand vom Anfang des Rings
// samples:    ausgegebene Abtastwerte seit dem Einschalten (laeuft bei 2^32 ueber)
// seq_errors: Spruenge, wenn man {Q, I} als 16-Bit-Zaehler liest; nur fuer den Test mit
//             Zaehlerdaten von Bedeutung
void iq_reader(ap_uint<1> enable, ap_uint<32> ring_addr, ap_uint<32> ring_bytes, ap_uint<32> wr_ptr,
               ap_uint<32> *rd_ptr, ap_uint<32> *samples, ap_uint<32> *seq_errors,
               ap_uint<64> *ring,
               hls::stream<ap_uint<16> > &m_axis_iq);

#endif
