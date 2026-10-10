# =============================================================================
#  RTL-Co-Simulation der Cores mit AXI-Master
#     pattern_gen  Framebuffer-Leser, mit kleinem Bild (128x16), damit es in Minuten durchlaeuft
#     iq_reader    Leser fuer den I/Q-Ringpuffer
#
#  Aufruf:   vitis-run --mode hls --tcl cosim_hls.tcl
#            HLS_ONLY=iq_reader vitis-run --mode hls --tcl cosim_hls.tcl    (nur ein Core)
#  Ergebnis: je Core "TEST OK" und "C/RTL co-simulation finished: PASS" in der Ausgabe
# =============================================================================
cd [file dirname [file normalize [info script]]]/hls

set small "-DFRAME_WIDTH=128 -DFRAME_HEIGHT=16"

# Name, Compiler-Schalter fuer den Core, zusaetzliche fuer die Testbench
set cores [list \
    pattern_gen $small "-DCOSIM" \
    iq_reader   ""     "" \
]

foreach {core cflags tb_cflags} $cores {
    if {[info exists ::env(HLS_ONLY)] && $::env(HLS_ONLY) ne $core} { continue }
    open_project -reset proj_${core}_cosim
    set_top $core
    add_files $core.cpp -cflags $cflags
    add_files -tb ${core}_tb.cpp -cflags "$cflags $tb_cflags"

    open_solution -reset solution1 -flow_target vivado
    set_part xc7z020clg484-1
    create_clock -period 6.7 -name default
    config_interface -m_axi_addr64=false

    csim_design
    csynth_design
    cosim_design
    close_project
}
exit
