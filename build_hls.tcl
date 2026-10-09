# =============================================================================
#  HLS-Cores -> Vivado-IP
#     sine_gen     Sinus-Generator (DDS), Frequenz ueber AXI-Lite
#     pattern_gen  HDMI-Testbild-Generator, Bildauswahl ueber AXI-Lite
#
#  Aufruf:   vitis-run --mode hls --tcl build_hls.tcl
#  Ergebnis: ./hls/proj_<core>/solution1/impl/ip   (IP-Repositories fuer create_hw.tcl)
# =============================================================================
cd [file dirname [file normalize [info script]]]/hls

# Name, Taktperiode in ns, Beschreibung
set cores {
    sine_gen    10.0  "Sinus-Generator (DDS), Frequenz ueber AXI-Lite"
    pattern_gen 13.0  "Testbild-Generator 1280x720 YCbCr 4:2:2, Auswahl ueber AXI-Lite"
}

foreach {core period description} $cores {
    open_project -reset proj_$core
    set_top $core
    add_files $core.cpp
    add_files -tb ${core}_tb.cpp

    open_solution -reset solution1 -flow_target vivado
    set_part xc7z020clg484-1
    create_clock -period $period -name default

    csim_design
    csynth_design
    export_design -format ip_catalog -vendor user.org -library hls -version 1.0 \
        -description $description
    close_project
}
exit
