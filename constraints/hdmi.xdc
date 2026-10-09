# ADV7511 HDMI-Sender auf dem ZedBoard (Bank 33, 3,3 V)
# Pins laut Zedboard-Master.xdc (Digilent)
set_property PACKAGE_PIN W18  [get_ports hd_clk]
set_property PACKAGE_PIN U16  [get_ports hd_de]
set_property PACKAGE_PIN V17  [get_ports hd_hsync]
set_property PACKAGE_PIN W17  [get_ports hd_vsync]

# 16 Bit YCbCr 4:2:2: D[7:0] = Y, D[15:8] = Cb/Cr
set_property PACKAGE_PIN Y13  [get_ports {hd_d[0]}]
set_property PACKAGE_PIN AA13 [get_ports {hd_d[1]}]
set_property PACKAGE_PIN AA14 [get_ports {hd_d[2]}]
set_property PACKAGE_PIN Y14  [get_ports {hd_d[3]}]
set_property PACKAGE_PIN AB15 [get_ports {hd_d[4]}]
set_property PACKAGE_PIN AB16 [get_ports {hd_d[5]}]
set_property PACKAGE_PIN AA16 [get_ports {hd_d[6]}]
set_property PACKAGE_PIN AB17 [get_ports {hd_d[7]}]
set_property PACKAGE_PIN AA17 [get_ports {hd_d[8]}]
set_property PACKAGE_PIN Y15  [get_ports {hd_d[9]}]
set_property PACKAGE_PIN W13  [get_ports {hd_d[10]}]
set_property PACKAGE_PIN W15  [get_ports {hd_d[11]}]
set_property PACKAGE_PIN V15  [get_ports {hd_d[12]}]
set_property PACKAGE_PIN U17  [get_ports {hd_d[13]}]
set_property PACKAGE_PIN V14  [get_ports {hd_d[14]}]
set_property PACKAGE_PIN V13  [get_ports {hd_d[15]}]

# I2C (PS-I2C1 ueber EMIO)
set_property PACKAGE_PIN AA18 [get_ports hd_iic_scl_io]
set_property PACKAGE_PIN Y16  [get_ports hd_iic_sda_io]
set_property PULLUP true [get_ports {hd_iic_scl_io hd_iic_sda_io}]

set_property IOSTANDARD LVCMOS33 [get_ports {hd_clk hd_de hd_hsync hd_vsync hd_d[*] hd_iic_scl_io hd_iic_sda_io}]

# Ausgangsregister in die IO-Zellen -> gleiche Laufzeit fuer alle Datenleitungen
set_property IOB TRUE [get_ports {hd_de hd_hsync hd_vsync hd_d[*]}]
