# =============================================================================
#  HLS-Cores -> Vivado-IP
#     sine_gen     Sinus-Generator (DDS), Frequenz ueber AXI-Lite
#     pattern_gen  HDMI-Testbild-Generator mit Framebuffer-Leser, Bildauswahl ueber AXI-Lite
#     iq_reader    Leser fuer den I/Q-Ringpuffer im DDR, Ausgabe als AXI4-Stream
#     iq_spectrum  Spektrum der I/Q-Daten: FFT mit 1024 Punkten, Blockmittelung, Max-Hold
#
#  Aufruf:   vitis-run --mode hls --tcl build_hls.tcl
#            HLS_ONLY=iq_reader vitis-run --mode hls --tcl build_hls.tcl    (nur ein Core)
#  Ergebnis: ./hls/proj_<core>/solution1/impl/ip   (IP-Repositories fuer create_hw.tcl)
# =============================================================================
cd [file dirname [file normalize [info script]]]/hls

# Name, Taktperiode in ns, Beschreibung
set cores {
    sine_gen    10.0  "Sinus-Generator (DDS), Frequenz ueber AXI-Lite"
    pattern_gen 6.7   "Testbild-Generator 1920x1080 YCbCr 4:2:2 mit Framebuffer-Leser, Auswahl ueber AXI-Lite"
    iq_reader   6.7   "Leser fuer den I/Q-Ringpuffer im DDR, Ausgabe als AXI4-Stream"
    iq_spectrum 6.7   "Spektrum der I/Q-Daten: FFT 1024, Blockmittelung, Max-Hold"
}

foreach {core period description} $cores {
    if {[info exists ::env(HLS_ONLY)] && $::env(HLS_ONLY) ne $core} { continue }
    open_project -reset proj_$core
    set_top $core
    add_files $core.cpp
    add_files -tb ${core}_tb.cpp

    open_solution -reset solution1 -flow_target vivado
    set_part xc7z020clg484-1
    create_clock -period $period -name default
    config_interface -m_axi_addr64=false           ;# Speicheradressen sind 32 Bit breit

    csim_design -O                                 ;# optimiert uebersetzt, sonst laufen die Bildtests sehr lange
    csynth_design
    export_design -format ip_catalog -vendor user.org -library hls -version 1.0 \
        -description $description
    close_project
}
exit
