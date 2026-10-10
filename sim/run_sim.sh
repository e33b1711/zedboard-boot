#!/usr/bin/env bash
# Simulation mit dem Vivado-Simulator (vorher settings64.sh von Vivado laden)
#   ./run_sim.sh             rtl/i2s_tx.v
#   ./run_sim.sh iq_scope    rtl/iq_scope.v (I/Q-Oszilloskop), schreibt work/bild_*.txt
#   ./run_sim.sh spec_trace  rtl/spec_trace.v (Spektrum als Kurve)
set -euo pipefail
cd "$(dirname "$0")"
# xelab linkt mit dem System-ld; ohne diesen Pfad findet er crt1.o nicht (Mint/Ubuntu)
export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}
mkdir -p work && cd work
case "${1:-i2s_tx}" in
i2s_tx)
    xvlog ../../rtl/i2s_tx.v ../i2s_tx_tb.v "$XILINX_VIVADO/data/verilog/src/glbl.v"
    xelab -L unisims_ver i2s_tx_tb glbl -s i2s_tx_sim
    xsim i2s_tx_sim -runall ;;
iq_scope)
    xvlog ../../rtl/iq_scope.v ../../rtl/iq_palette.v ../iq_scope_tb.v
    xelab iq_scope_tb -s iq_scope_sim
    xsim iq_scope_sim -runall ;;
spec_trace)
    xvlog ../../rtl/spec_trace.v ../spec_trace_tb.v
    xelab spec_trace_tb -s spec_trace_sim
    xsim spec_trace_sim -runall ;;
*)  echo "Aufruf: $0 [i2s_tx|iq_scope|spec_trace]" >&2; exit 1 ;;
esac
