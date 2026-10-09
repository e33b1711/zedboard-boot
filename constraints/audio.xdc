# ADAU1761 Audio-Codec auf dem ZedBoard (Bank 13, 3,3 V)
# Pins laut Zedboard-Master.xdc (Digilent)
set_property -dict {PACKAGE_PIN AB2 IOSTANDARD LVCMOS33} [get_ports ac_mclk]
set_property -dict {PACKAGE_PIN AA6 IOSTANDARD LVCMOS33} [get_ports ac_bclk]
set_property -dict {PACKAGE_PIN Y6  IOSTANDARD LVCMOS33} [get_ports ac_lrclk]
# AC-GPIO0 = serielle Daten zum DAC
set_property -dict {PACKAGE_PIN Y8  IOSTANDARD LVCMOS33} [get_ports ac_sdata]

# I2C (PS-I2C0 ueber EMIO)
set_property -dict {PACKAGE_PIN AB4 IOSTANDARD LVCMOS33 PULLUP true} [get_ports ac_iic_scl_io]
set_property -dict {PACKAGE_PIN AB5 IOSTANDARD LVCMOS33 PULLUP true} [get_ports ac_iic_sda_io]

# I2C-Adresswahl: beide 0 -> Adresse 0x38
set_property -dict {PACKAGE_PIN AB1 IOSTANDARD LVCMOS33} [get_ports {ac_addr[0]}]
set_property -dict {PACKAGE_PIN Y5  IOSTANDARD LVCMOS33} [get_ports {ac_addr[1]}]
