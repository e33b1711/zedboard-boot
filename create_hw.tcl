# =============================================================================
#  ZedBoard – Hardware-Projekt from scratch (Vivado 2024.x / 2025.x)
#
#  Aufruf:  vivado -mode batch -source create_hw.tcl
#  Ergebnis: ./system.xsa  (inkl. Bitstream) fuer PetaLinux
#
#  Inhalt:  Zynq PS7 mit ZedBoard-Preset (DDR, MIO, UART, SD, ETH, USB)
#           + AXI-GPIO an den 8 User-LEDs (damit der PL-Teil etwas tut)
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
