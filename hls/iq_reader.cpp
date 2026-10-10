#include "iq_reader.h"

// Ein Aufruf = hoechstens ein Block: liegt einer bereit, wird er in einem Zug aus dem
// Speicher gelesen und dann Abtastwert fuer Abtastwert ausgegeben
void iq_reader(ap_uint<1> enable, ap_uint<32> ring_addr, ap_uint<32> ring_bytes, ap_uint<32> wr_ptr,
               ap_uint<32> *rd_ptr, ap_uint<32> *samples, ap_uint<32> *seq_errors,
               ap_uint<64> *ring,
               hls::stream<ap_uint<16> > &m_axis_iq) {
#pragma HLS INTERFACE s_axilite port=enable     bundle=ctrl
#pragma HLS INTERFACE s_axilite port=ring_addr  bundle=ctrl
#pragma HLS INTERFACE s_axilite port=ring_bytes bundle=ctrl
#pragma HLS INTERFACE s_axilite port=wr_ptr     bundle=ctrl
#pragma HLS INTERFACE s_axilite port=rd_ptr     bundle=ctrl
#pragma HLS INTERFACE s_axilite port=samples    bundle=ctrl
#pragma HLS INTERFACE s_axilite port=seq_errors bundle=ctrl
// Bursts hoechstens 16 Worte lang: der HP-Port des PS spricht AXI3
#pragma HLS INTERFACE m_axi port=ring bundle=ring offset=off depth=IQ_MEM_WORDS max_read_burst_length=16 num_read_outstanding=4
#pragma HLS INTERFACE axis port=m_axis_iq
// Laeuft frei: ap_start liegt im Block Design fest auf 1
#pragma HLS INTERFACE ap_ctrl_hs port=return

    static ap_uint<32> rd = 0;             // Lesezeiger, immer am Blockanfang
    static ap_uint<32> count = 0;
    static ap_uint<32> errors = 0;
    static ap_uint<16> last = 0;           // letzter Abtastwert fuer die Zaehlerpruefung
    static bool have_last = false;

    ap_uint<64> block[IQ_CHUNK_WORDS];
    ap_uint<32> mask = ring_bytes - 1;

    if (!enable) {
        rd = wr_ptr & mask & ~ap_uint<32>(IQ_CHUNK_BYTES - 1);
        count = 0;
        errors = 0;
        have_last = false;
    } else if (((wr_ptr - rd) & mask) >= IQ_CHUNK_BYTES) {
        ap_uint<29> base = (ring_addr + rd) >> 3;      // Wortadresse
        for (int i = 0; i < IQ_CHUNK_WORDS; i++) {
#pragma HLS PIPELINE II=1
            block[i] = ring[base + i];
        }
        for (int i = 0; i < IQ_CHUNK_SAMPLES; i++) {
#pragma HLS PIPELINE II=1
            ap_uint<64> word = block[i >> 2];
            ap_uint<16> s = word >> (16 * (i & 3));
            m_axis_iq.write(s);
            if (have_last && s != ap_uint<16>(last + 1)) errors++;
            last = s;
            have_last = true;
        }
        count += IQ_CHUNK_SAMPLES;
        rd = (rd + IQ_CHUNK_BYTES) & mask;
    }

    *rd_ptr = rd;
    *samples = count;
    *seq_errors = errors;
}
