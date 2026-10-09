#!/usr/bin/env bash
# Simulation von rtl/i2s_tx.v mit dem Vivado-Simulator (vorher settings64.sh von Vivado laden)
set -euo pipefail
cd "$(dirname "$0")"
# xelab linkt mit dem System-ld; ohne diesen Pfad findet er crt1.o nicht (Mint/Ubuntu)
export LIBRARY_PATH=/usr/lib/x86_64-linux-gnu${LIBRARY_PATH:+:$LIBRARY_PATH}
mkdir -p work && cd work
xvlog ../../rtl/i2s_tx.v ../i2s_tx_tb.v "$XILINX_VIVADO/data/verilog/src/glbl.v"
xelab -L unisims_ver i2s_tx_tb glbl -s i2s_tx_sim
xsim i2s_tx_sim -runall
