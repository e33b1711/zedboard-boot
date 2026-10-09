# =============================================================================
#  Sinus-Generator (HLS) -> Vivado-IP
#
#  Aufruf:   vitis-run --mode hls --tcl build_hls.tcl
#  Ergebnis: ./hls/proj/solution1/impl/ip   (IP-Repository fuer create_hw.tcl)
# =============================================================================
cd [file dirname [file normalize [info script]]]/hls

open_project -reset proj
set_top sine_gen
add_files sine_gen.cpp
add_files -tb sine_gen_tb.cpp

open_solution -reset solution1 -flow_target vivado
set_part xc7z020clg484-1
create_clock -period 10 -name default

csim_design
csynth_design
export_design -format ip_catalog -vendor user.org -library hls -version 1.0 \
    -description "Sinus-Generator (DDS), Frequenz ueber AXI-Lite"
exit
