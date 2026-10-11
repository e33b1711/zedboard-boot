# =============================================================================
#  ZedBoard – Hardware-Projekt from scratch (Vivado 2024.x / 2025.x)
#
#  Aufruf:  vivado -mode batch -source create_hw.tcl
#  Ergebnis: ./system.xsa  (inkl. Bitstream) fuer PetaLinux
#
#  Vorher:  vitis-run --mode hls --tcl build_hls.tcl   (erzeugt die HLS-IPs)
#
#  Inhalt:  Zynq PS7 mit ZedBoard-Preset (DDR, MIO, UART, SD, ETH, USB)
#           + AXI-GPIO an den 8 User-LEDs
#           + Sinuston auf Line Out: HLS-Sinus-Generator -> I2S -> ADAU1761
#           + HDMI-Testbild 1080p60: HLS-Testbild-Generator -> Video Out -> ADV7511
#           + drei Framebuffer im DDR, vom Testbild-Generator ueber den HP0-Port gelesen
#           + I/Q-Oszilloskop als Einblendung im Videostrom (Bildspeicher im Block-RAM)
#           + I/Q-Ringpuffer im DDR, vom Core iq_reader ueber den HP1-Port gelesen
#           + Spektrum der I/Q-Daten (FFT, Mittelung, Max-Hold), als Kurve im Oszilloskop
#           + Messung der Interrupt-Laufzeit: irq_timer setzt den IRQ, Linux setzt ihn zurueck
#           + OLED-Anzeige (SSD1306) ueber AXI Quad SPI und ein AXI-GPIO
# =============================================================================

set proj_name zedboard_hw
set proj_dir  [file normalize ./vivado]
set xsa_out   [file normalize ./system.xsa]
set part      xc7z020clg484-1
set jobs      8

# --- Hilfsfunktion: neueste installierte IP-Version finden ------------------
proc latest_ip {name} {
    set defs [lsort [get_ipdefs -all -filter "NAME == $name"]]
    if {[llength $defs] == 0} { error "IP '$name' nicht gefunden" }
    return [lindex $defs end]
}

# --- Board-Dateien sicherstellen (ZedBoard kommt aus dem AMD Board Store) ---
# Im Batch-Modus wird das lokale Board-Store-Verzeichnis nicht automatisch durchsucht
set board_store [xhub::get_xstores xilinx_board_store]
set_param board.repoPaths [list [get_property LOCAL_ROOT_DIR $board_store]]
set bp [lindex [get_board_parts -quiet -latest_file_version *zedboard*] 0]
if {$bp eq ""} {
    puts "INFO: ZedBoard-Boardfiles fehlen – versuche Installation aus dem Board Store ..."
    if {[catch {
        xhub::refresh_catalog $board_store
        xhub::install [xhub::get_xitems -of_objects $board_store *zedboard*]
    } err]} {
        puts "WARN: Automatische Installation fehlgeschlagen: $err"
    }
    set_param board.repoPaths [list [get_property LOCAL_ROOT_DIR $board_store]]
    set bp [lindex [get_board_parts -quiet -latest_file_version *zedboard*] 0]
}
if {$bp eq ""} {
    error "Keine ZedBoard-Boardfiles verfuegbar. In der GUI: Tools > Vivado Store > Boards > ZedBoard installieren."
}
puts "INFO: Verwende Board-Part $bp"

# --- Projekt ----------------------------------------------------------------
create_project $proj_name $proj_dir -part $part -force
set_property board_part $bp [current_project]

# --- Eigene Quellen: HLS-IP, Verilog, Constraints ---------------------------
set hls_ips {}
foreach core {sine_gen pattern_gen iq_reader iq_spectrum} {
    set hls_ip [file normalize ./hls/proj_$core/solution1/impl/ip]
    if {![file exists $hls_ip/component.xml]} {
        error "HLS-IP fehlt ($hls_ip). Zuerst: vitis-run --mode hls --tcl build_hls.tcl"
    }
    lappend hls_ips $hls_ip
}
set_property ip_repo_paths $hls_ips [current_project]
update_ip_catalog
add_files -norecurse [list [file normalize ./rtl/i2s_tx.v] [file normalize ./rtl/clk_fwd.v] \
    [file normalize ./rtl/iq_scope.v] [file normalize ./rtl/iq_palette.v] [file normalize ./rtl/spec_trace.v] \
    [file normalize ./rtl/irq_timer.v] [file normalize ./rtl/oled_mux.v]]
add_files -fileset constrs_1 -norecurse [list [file normalize ./constraints/audio.xdc] [file normalize ./constraints/hdmi.xdc] \
    [file normalize ./constraints/oled.xdc]]
update_compile_order -fileset sources_1

# --- Block Design -----------------------------------------------------------
create_bd_design system

create_bd_cell -type ip -vlnv [latest_ip processing_system7] ps7
apply_bd_automation -rule xilinx.com:bd_rule:processing_system7 \
    -config {make_external "FIXED_IO, DDR" apply_board_preset "1" Master "Disable" Slave "Disable"} \
    [get_bd_cells ps7]

# AXI-GPIO fuer die 8 LEDs (LD0..LD7)
create_bd_cell -type ip -vlnv [latest_ip axi_gpio] leds
apply_bd_automation -rule xilinx.com:bd_rule:board \
    -config {Board_Interface "leds_8bits" Manual_Source "Auto"} \
    [get_bd_intf_pins leds/GPIO]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins leds/S_AXI]

# --- Audio: Sinus-Generator (HLS) -> I2S-Sender -> ADAU1761 (Line Out) -------
#   Frequenz: AXI-Lite-Register phase_inc des HLS-Cores (Offset 0x10)
#   Takte:    HLS-Core an FCLK0 (100 MHz), I2S an 12,288 MHz (= 256 * 48 kHz)
set fclk   [get_bd_pins ps7/FCLK_CLK0]
set rst100 [get_bd_cells -filter {VLNV =~ "*:proc_sys_reset:*"}]

# Codec-I2C: PS-I2C0 ueber EMIO auf PL-Pins
set_property -dict [list CONFIG.PCW_I2C0_PERIPHERAL_ENABLE {1} CONFIG.PCW_I2C0_I2C0_IO {EMIO}] [get_bd_cells ps7]
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 ac_iic
connect_bd_intf_net [get_bd_intf_pins ps7/IIC_0] [get_bd_intf_ports ac_iic]

# I2C-Adresspins des Codecs fest auf 0 (Adresse 0x38)
create_bd_cell -type ip -vlnv [latest_ip xlconstant] ac_addr_const
set_property -dict [list CONFIG.CONST_WIDTH {2} CONFIG.CONST_VAL {0}] [get_bd_cells ac_addr_const]
create_bd_port -dir O -from 1 -to 0 ac_addr
connect_bd_net [get_bd_pins ac_addr_const/dout] [get_bd_ports ac_addr]

# Audio-Takt 12,288 MHz und zugehoeriger Reset
create_bd_cell -type ip -vlnv [latest_ip clk_wiz] audio_clk
set_property -dict [list \
    CONFIG.PRIM_SOURCE {Global_buffer} \
    CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {12.288} \
    CONFIG.USE_RESET {false} \
    CONFIG.USE_LOCKED {true}] [get_bd_cells audio_clk]
connect_bd_net $fclk [get_bd_pins audio_clk/clk_in1]
set aclk [get_bd_pins audio_clk/clk_out1]

create_bd_cell -type ip -vlnv [latest_ip proc_sys_reset] audio_rst
connect_bd_net $aclk [get_bd_pins audio_rst/slowest_sync_clk]
connect_bd_net [get_bd_pins ps7/FCLK_RESET0_N] [get_bd_pins audio_rst/ext_reset_in]
connect_bd_net [get_bd_pins audio_clk/locked] [get_bd_pins audio_rst/dcm_locked]

# Sinus-Generator (HLS-IP aus build_hls.tcl)
create_bd_cell -type ip -vlnv [latest_ip sine_gen] sine_gen
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins sine_gen/s_axi_ctrl]

# I2S-Sender (Verilog) im Audio-Takt
create_bd_cell -type module -reference i2s_tx i2s_tx
connect_bd_net $aclk [get_bd_pins i2s_tx/aclk]
connect_bd_net [get_bd_pins audio_rst/peripheral_aresetn] [get_bd_pins i2s_tx/aresetn]

# Samples vom 100-MHz- in den Audio-Takt
create_bd_cell -type ip -vlnv [latest_ip axis_clock_converter] axis_cdc
connect_bd_net $fclk [get_bd_pins axis_cdc/s_axis_aclk]
connect_bd_net [get_bd_pins $rst100/peripheral_aresetn] [get_bd_pins axis_cdc/s_axis_aresetn]
connect_bd_net $aclk [get_bd_pins axis_cdc/m_axis_aclk]
connect_bd_net [get_bd_pins audio_rst/peripheral_aresetn] [get_bd_pins axis_cdc/m_axis_aresetn]
connect_bd_intf_net [get_bd_intf_pins sine_gen/m_axis] [get_bd_intf_pins axis_cdc/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins axis_cdc/M_AXIS] [get_bd_intf_pins i2s_tx/s_axis]

# Codec-Pins
foreach {port pin} {ac_mclk i2s_mclk ac_bclk i2s_bclk ac_lrclk i2s_lrclk ac_sdata i2s_sdata} {
    create_bd_port -dir O $port
    connect_bd_net [get_bd_pins i2s_tx/$pin] [get_bd_ports $port]
}

# --- HDMI: Testbild-Generator (HLS) -> I/Q-Oszilloskop -> Video Out -> ADV7511 ---
#   Bildauswahl: AXI-Lite-Register des HLS-Cores (pattern 0x10, color 0x18, format 0x20)
#   Framebuffer: der Core liest ueber seinen AXI-Master aus dem DDR des PS (HP0-Port)
#   1920x1080 bei 60 Hz, Pixeltakt 148,5 MHz, 16 Bit YCbCr 4:2:2
#   Kein Linux-Treiber: der ADV7511 wird per Skript ueber I2C eingerichtet

# ADV7511-I2C: PS-I2C1 ueber EMIO auf PL-Pins
set_property -dict [list CONFIG.PCW_I2C1_PERIPHERAL_ENABLE {1} CONFIG.PCW_I2C1_I2C1_IO {EMIO}] [get_bd_cells ps7]
create_bd_intf_port -mode Master -vlnv xilinx.com:interface:iic_rtl:1.0 hd_iic
connect_bd_intf_net [get_bd_intf_pins ps7/IIC_1] [get_bd_intf_ports hd_iic]

# Pixeltakt 148,5 MHz und zugehoeriger Reset
create_bd_cell -type ip -vlnv [latest_ip clk_wiz] video_clk
set_property -dict [list \
    CONFIG.PRIM_SOURCE {Global_buffer} \
    CONFIG.PRIM_IN_FREQ {100.000} \
    CONFIG.CLKOUT1_REQUESTED_OUT_FREQ {148.5} \
    CONFIG.USE_RESET {false} \
    CONFIG.USE_LOCKED {true}] [get_bd_cells video_clk]
connect_bd_net $fclk [get_bd_pins video_clk/clk_in1]
set pclk [get_bd_pins video_clk/clk_out1]

create_bd_cell -type ip -vlnv [latest_ip proc_sys_reset] video_rst
connect_bd_net $pclk [get_bd_pins video_rst/slowest_sync_clk]
connect_bd_net [get_bd_pins ps7/FCLK_RESET0_N] [get_bd_pins video_rst/ext_reset_in]
connect_bd_net [get_bd_pins video_clk/locked] [get_bd_pins video_rst/dcm_locked]
set prstn [get_bd_pins video_rst/peripheral_aresetn]

# Testbild-Generator (HLS-IP) im Pixeltakt; Takt vor der Automation anschliessen,
# damit der AXI-Interconnect den Taktuebergang zum 100-MHz-Bus uebernimmt
create_bd_cell -type ip -vlnv [latest_ip pattern_gen] pattern_gen
connect_bd_net $pclk  [get_bd_pins pattern_gen/ap_clk]
connect_bd_net $prstn [get_bd_pins pattern_gen/ap_rst_n]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins pattern_gen/s_axi_ctrl]
if {[get_bd_nets -of_objects [get_bd_pins pattern_gen/ap_clk]] ne [get_bd_nets -of_objects $pclk]} {
    error "pattern_gen laeuft nicht im Pixeltakt"
}

# Der Core laeuft frei: ap_start fest auf 1
create_bd_cell -type ip -vlnv [latest_ip xlconstant] pattern_start
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] [get_bd_cells pattern_start]
connect_bd_net [get_bd_pins pattern_start/dout] [get_bd_pins pattern_gen/ap_start]

# Framebuffer: AXI-Master des Cores (AXI4, Pixeltakt) -> HP0-Port des PS (AXI3, 100 MHz).
# Der SmartConnect uebernimmt Taktuebergang und Protokoll; der HP-Port bleibt auf 100 MHz,
# damit die Pfade ins PS nicht am knappen Pixeltakt haengen.
set_property -dict [list CONFIG.PCW_USE_S_AXI_HP0 {1}] [get_bd_cells ps7]
connect_bd_net $fclk [get_bd_pins ps7/S_AXI_HP0_ACLK]
create_bd_cell -type ip -vlnv [latest_ip smartconnect] fb_axi
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {2}] [get_bd_cells fb_axi]
connect_bd_net $pclk  [get_bd_pins fb_axi/aclk]
connect_bd_net $fclk  [get_bd_pins fb_axi/aclk1]
connect_bd_net $prstn [get_bd_pins fb_axi/aresetn]
connect_bd_intf_net [get_bd_intf_pins pattern_gen/m_axi_fb] [get_bd_intf_pins fb_axi/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins fb_axi/M00_AXI] [get_bd_intf_pins ps7/S_AXI_HP0]

# I/Q-Daten: Linux schreibt sie in einen Ringpuffer im DDR, der Core iq_reader (HLS-IP)
# liest sie ueber den HP1-Port und gibt sie als Strom an das Oszilloskop.
#   Register: enable 0x10, ring_addr 0x18, ring_bytes 0x20, wr_ptr 0x28,
#             rd_ptr 0x30, samples 0x40, seq_errors 0x50
#   Eigener HP-Port, damit der Framebuffer-Leser am HP0 nicht warten muss.
create_bd_cell -type ip -vlnv [latest_ip iq_reader] iq_reader
connect_bd_net $pclk  [get_bd_pins iq_reader/ap_clk]
connect_bd_net $prstn [get_bd_pins iq_reader/ap_rst_n]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins iq_reader/s_axi_ctrl]
if {[get_bd_nets -of_objects [get_bd_pins iq_reader/ap_clk]] ne [get_bd_nets -of_objects $pclk]} {
    error "iq_reader laeuft nicht im Pixeltakt"
}
connect_bd_net [get_bd_pins pattern_start/dout] [get_bd_pins iq_reader/ap_start]

set_property -dict [list CONFIG.PCW_USE_S_AXI_HP1 {1}] [get_bd_cells ps7]
connect_bd_net $fclk [get_bd_pins ps7/S_AXI_HP1_ACLK]
create_bd_cell -type ip -vlnv [latest_ip smartconnect] iq_axi
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1} CONFIG.NUM_CLKS {2}] [get_bd_cells iq_axi]
connect_bd_net $pclk  [get_bd_pins iq_axi/aclk]
connect_bd_net $fclk  [get_bd_pins iq_axi/aclk1]
connect_bd_net $prstn [get_bd_pins iq_axi/aresetn]
connect_bd_intf_net [get_bd_intf_pins iq_reader/m_axi_ring] [get_bd_intf_pins iq_axi/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins iq_axi/M00_AXI] [get_bd_intf_pins ps7/S_AXI_HP1]

# Timing-Generator 1080p60 (feste Einstellung, ohne AXI-Lite)
create_bd_cell -type ip -vlnv [latest_ip v_tc] video_tc
set_property -dict [list \
    CONFIG.VIDEO_MODE {1080p} \
    CONFIG.HAS_AXI4_LITE {false} \
    CONFIG.enable_detection {false}] [get_bd_cells video_tc]
connect_bd_net $pclk  [get_bd_pins video_tc/clk]
connect_bd_net $prstn [get_bd_pins video_tc/resetn]

# AXI4-Stream -> Video mit Sync-Signalen; bremst den Generator ueber tready
create_bd_cell -type ip -vlnv [latest_ip v_axi4s_vid_out] video_out
set_property -dict [list \
    CONFIG.C_S_AXIS_VIDEO_FORMAT {0} \
    CONFIG.C_S_AXIS_VIDEO_DATA_WIDTH {8} \
    CONFIG.C_NATIVE_COMPONENT_WIDTH {8} \
    CONFIG.C_HAS_ASYNC_CLK {0} \
    CONFIG.C_VTG_MASTER_SLAVE {0}] [get_bd_cells video_out]
if {[get_property LEFT [get_bd_pins video_out/vid_data]] != 15} {
    error "video_out: Datenbreite ist nicht 16 Bit"
}
connect_bd_net $pclk  [get_bd_pins video_out/aclk]
connect_bd_net $prstn [get_bd_pins video_out/aresetn]

# I/Q-Oszilloskop (Verilog) zwischen Testbild-Generator und Videoausgabe.
#   Einstellung ueber ein AXI-GPIO mit 32 Ausgaengen (Belegung siehe rtl/iq_scope.v);
#   das GPIO laeuft im Pixeltakt, damit das Steuerwort ohne Taktuebergang ankommt.
#   Den I/Q-Eingang speist spec_trace: entweder die Daten von iq_reader oder die Kurve
#   des Spektrums.
create_bd_cell -type ip -vlnv [latest_ip axi_gpio] scope_ctrl
set_property -dict [list CONFIG.C_GPIO_WIDTH {32} CONFIG.C_ALL_OUTPUTS {1} CONFIG.C_DOUT_DEFAULT {0x00000000}] \
    [get_bd_cells scope_ctrl]
connect_bd_net $pclk  [get_bd_pins scope_ctrl/s_axi_aclk]
connect_bd_net $prstn [get_bd_pins scope_ctrl/s_axi_aresetn]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins scope_ctrl/S_AXI]
if {[get_bd_nets -of_objects [get_bd_pins scope_ctrl/s_axi_aclk]] ne [get_bd_nets -of_objects $pclk]} {
    error "scope_ctrl laeuft nicht im Pixeltakt"
}

create_bd_cell -type module -reference iq_scope iq_scope
connect_bd_net $pclk  [get_bd_pins iq_scope/aclk]
connect_bd_net $prstn [get_bd_pins iq_scope/aresetn]
connect_bd_net [get_bd_pins scope_ctrl/gpio_io_o] [get_bd_pins iq_scope/ctrl]

connect_bd_intf_net [get_bd_intf_pins pattern_gen/m_axis_video] [get_bd_intf_pins iq_scope/s_axis_video]
connect_bd_intf_net [get_bd_intf_pins iq_scope/m_axis_video] [get_bd_intf_pins video_out/video_in]

# Spektrum: die I/Q-Daten gehen zusaetzlich in den Core iq_spectrum (HLS-IP: Fenster, FFT mit
# 1024 Punkten, Blockmittelung der Leistung, Max-Hold). spec_trace (Verilog) zeichnet das
# Ergebnis als Kurve in das Oszilloskop oder reicht die I/Q-Daten durch (ctrl[23]).
#   Register von iq_spectrum: avg_log2 0x10, maxhold 0x18, spectra 0x20, overflows 0x30
create_bd_cell -type ip -vlnv [latest_ip axis_broadcaster] iq_split
set_property -dict [list \
    CONFIG.NUM_MI {2} \
    CONFIG.S_TDATA_NUM_BYTES {2} \
    CONFIG.M_TDATA_NUM_BYTES {2} \
    CONFIG.M00_TDATA_REMAP {tdata[15:0]} \
    CONFIG.M01_TDATA_REMAP {tdata[15:0]}] [get_bd_cells iq_split]
connect_bd_net $pclk  [get_bd_pins iq_split/aclk]
connect_bd_net $prstn [get_bd_pins iq_split/aresetn]

create_bd_cell -type ip -vlnv [latest_ip iq_spectrum] iq_spectrum
connect_bd_net $pclk  [get_bd_pins iq_spectrum/ap_clk]
connect_bd_net $prstn [get_bd_pins iq_spectrum/ap_rst_n]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins iq_spectrum/s_axi_ctrl]
if {[get_bd_nets -of_objects [get_bd_pins iq_spectrum/ap_clk]] ne [get_bd_nets -of_objects $pclk]} {
    error "iq_spectrum laeuft nicht im Pixeltakt"
}
connect_bd_net [get_bd_pins pattern_start/dout] [get_bd_pins iq_spectrum/ap_start]

create_bd_cell -type module -reference spec_trace spec_trace
connect_bd_net $pclk  [get_bd_pins spec_trace/aclk]
connect_bd_net $prstn [get_bd_pins spec_trace/aresetn]
connect_bd_net [get_bd_pins scope_ctrl/gpio_io_o] [get_bd_pins spec_trace/ctrl]

connect_bd_intf_net [get_bd_intf_pins iq_reader/m_axis_iq]     [get_bd_intf_pins iq_split/S_AXIS]
connect_bd_intf_net [get_bd_intf_pins iq_split/M00_AXIS]       [get_bd_intf_pins spec_trace/s_axis_iq]
connect_bd_intf_net [get_bd_intf_pins iq_split/M01_AXIS]       [get_bd_intf_pins iq_spectrum/s_axis_iq]
connect_bd_intf_net [get_bd_intf_pins iq_spectrum/m_axis_spec] [get_bd_intf_pins spec_trace/s_axis_spec]
connect_bd_intf_net [get_bd_intf_pins spec_trace/m_axis_xy]    [get_bd_intf_pins iq_scope/s_axis_iq]
connect_bd_intf_net [get_bd_intf_pins video_tc/vtiming_out] [get_bd_intf_pins video_out/vtiming_in]
connect_bd_net [get_bd_pins video_out/vtg_ce] [get_bd_pins video_tc/gen_clken]
connect_bd_net [get_bd_pins video_out/sof_state_out] [get_bd_pins video_tc/sof_state]

# Pixeltakt invertiert auf den Pin
create_bd_cell -type module -reference clk_fwd hd_clk_fwd
connect_bd_net $pclk [get_bd_pins hd_clk_fwd/clk]

# ADV7511-Pins
create_bd_port -dir O hd_clk
connect_bd_net [get_bd_pins hd_clk_fwd/clk_out] [get_bd_ports hd_clk]
create_bd_port -dir O -from 15 -to 0 hd_d
connect_bd_net [get_bd_pins video_out/vid_data] [get_bd_ports hd_d]
foreach {port pin} {hd_de vid_active_video hd_hsync vid_hsync hd_vsync vid_vsync} {
    create_bd_port -dir O $port
    connect_bd_net [get_bd_pins video_out/$pin] [get_bd_ports $port]
}

# --- Interrupt-Laufzeit: irq_timer (Verilog) setzt den IRQ, Linux quittiert ueber AXI-Lite ---
#   Laeuft an FCLK0 (100 MHz, 10 ns Aufloesung), also ohne Taktuebergang zwischen dem
#   Schreibzugriff und der Zeitmessung. Der IRQ geht an IRQ_F2P[0] des PS (SPI 61).
#   Register siehe rtl/irq_timer.v
set_property -dict [list CONFIG.PCW_USE_FABRIC_INTERRUPT {1} CONFIG.PCW_IRQ_F2P_INTR {1}] [get_bd_cells ps7]
create_bd_cell -type module -reference irq_timer irq_timer
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins irq_timer/s_axi]
if {[get_bd_nets -of_objects [get_bd_pins irq_timer/aclk]] ne [get_bd_nets -of_objects $fclk]} {
    error "irq_timer laeuft nicht an FCLK0"
}
connect_bd_net [get_bd_pins irq_timer/irq] [get_bd_pins ps7/IRQ_F2P]

# --- OLED-Anzeige (128 x 32, SSD1306): AXI Quad SPI + AXI-GPIO ---------------------
#   Der SPI-Core liefert nur Takt und Daten (Teiler 16 x 2: 100 MHz / 32 = 3,125 MHz, FIFO mit
#   256 Byte); die Anzeige hat keinen Chip Select und sendet nichts zurueck.
#   Das AXI-GPIO steuert die vier uebrigen Leitungen: Bit 0 D/C, 1 RES, 2 VBAT, 3 VDD
#   (RES, VBAT und VDD sind bei 0 aktiv; nach dem Reset ist alles aus). Mit Bit 4 kommen
#   Takt und Daten zur Fehlersuche aus Bit 5 und 6 statt vom SPI-Core.
#   Bedient werden beide von Linux aus ueber /dev/mem, siehe target/oled.h
create_bd_cell -type ip -vlnv [latest_ip axi_quad_spi] oled_spi
set_property -dict [list \
    CONFIG.C_USE_STARTUP {0} \
    CONFIG.C_NUM_SS_BITS {1} \
    CONFIG.C_SCK_RATIO {16} \
    CONFIG.Multiples16 {2} \
    CONFIG.C_FIFO_DEPTH {256}] [get_bd_cells oled_spi]
connect_bd_net $fclk [get_bd_pins oled_spi/ext_spi_clk]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins oled_spi/AXI_LITE]
if {[get_bd_nets -of_objects [get_bd_pins oled_spi/s_axi_aclk]] ne [get_bd_nets -of_objects $fclk]} {
    error "oled_spi laeuft nicht an FCLK0"
}
# Eingang fuer einen zweiten Master: fest auf 1, sonst meldet der Core einen Moduskonflikt
create_bd_cell -type ip -vlnv [latest_ip xlconstant] oled_ss_const
set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL {1}] [get_bd_cells oled_ss_const]
connect_bd_net [get_bd_pins oled_ss_const/dout] [get_bd_pins oled_spi/ss_i]
create_bd_cell -type ip -vlnv [latest_ip axi_gpio] oled_ctrl
set_property -dict [list CONFIG.C_GPIO_WIDTH {7} CONFIG.C_ALL_OUTPUTS {1} CONFIG.C_DOUT_DEFAULT {0x0000000E}] \
    [get_bd_cells oled_ctrl]
apply_bd_automation -rule xilinx.com:bd_rule:axi4 \
    -config {Master "/ps7/M_AXI_GP0" Clk "Auto"} \
    [get_bd_intf_pins oled_ctrl/S_AXI]

# Bits 4..6 des Steuerworts: Takt und Daten von Hand statt vom SPI-Core (rtl/oled_mux.v)
create_bd_cell -type module -reference oled_mux oled_mux
connect_bd_net [get_bd_pins oled_ctrl/gpio_io_o] [get_bd_pins oled_mux/ctrl]
connect_bd_net [get_bd_pins oled_spi/sck_o] [get_bd_pins oled_mux/spi_sck]
connect_bd_net [get_bd_pins oled_spi/io0_o] [get_bd_pins oled_mux/spi_sdo]
create_bd_port -dir O oled_sclk
create_bd_port -dir O oled_sdin
create_bd_port -dir O -from 3 -to 0 oled_gpio
connect_bd_net [get_bd_pins oled_mux/oled_sclk] [get_bd_ports oled_sclk]
connect_bd_net [get_bd_pins oled_mux/oled_sdin] [get_bd_ports oled_sdin]
connect_bd_net [get_bd_pins oled_mux/oled_gpio] [get_bd_ports oled_gpio]

assign_bd_address
validate_bd_design
save_bd_design

# --- Wrapper, Synthese, Implementierung, Bitstream --------------------------
set bd_file [get_files system.bd]
add_files -norecurse [make_wrapper -files $bd_file -top]
set_property top system_wrapper [current_fileset]
update_compile_order -fileset sources_1

launch_runs impl_1 -to_step write_bitstream -jobs $jobs
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {
    error "Implementierung/Bitstream fehlgeschlagen – siehe $proj_dir/$proj_name.runs/impl_1"
}

# --- Hardware-Plattform exportieren -----------------------------------------
write_hw_platform -fixed -include_bit -force -file $xsa_out
puts "FERTIG: $xsa_out"
