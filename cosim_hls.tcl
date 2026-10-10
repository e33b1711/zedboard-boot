# =============================================================================
#  RTL-Co-Simulation des Testbild-Generators (Framebuffer-Leser am AXI-Master)
#  mit kleinem Bild (128x16), damit die Simulation in Minuten durchlaeuft
#
#  Aufruf:   vitis-run --mode hls --tcl cosim_hls.tcl
#  Ergebnis: "TEST OK" und "C/RTL co-simulation finished: PASS" in der Ausgabe
# =============================================================================
cd [file dirname [file normalize [info script]]]/hls

set small "-DFRAME_WIDTH=128 -DFRAME_HEIGHT=16"

open_project -reset proj_pattern_gen_cosim
set_top pattern_gen
add_files pattern_gen.cpp -cflags $small
add_files -tb pattern_gen_tb.cpp -cflags "$small -DCOSIM"

open_solution -reset solution1 -flow_target vivado
set_part xc7z020clg484-1
create_clock -period 6.7 -name default
config_interface -m_axi_addr64=false

csim_design
csynth_design
cosim_design
close_project
exit
