# OLED-Anzeige auf dem ZedBoard (UG-2832HSWEG04, Controller SSD1306; Bank 13, 3,3 V)
# Pins laut Board-Dateien (Digilent zedboard 1.0, OLED2..OLED10 = Belegung des PmodOLED)
set_property -dict {PACKAGE_PIN AB12 IOSTANDARD LVCMOS33} [get_ports oled_sclk]
set_property -dict {PACKAGE_PIN AA12 IOSTANDARD LVCMOS33} [get_ports oled_sdin]
# D/C, RES, VBAT, VDD (RES, VBAT und VDD sind bei 0 aktiv)
set_property -dict {PACKAGE_PIN U10  IOSTANDARD LVCMOS33} [get_ports {oled_gpio[0]}]
set_property -dict {PACKAGE_PIN U9   IOSTANDARD LVCMOS33} [get_ports {oled_gpio[1]}]
set_property -dict {PACKAGE_PIN U11  IOSTANDARD LVCMOS33} [get_ports {oled_gpio[2]}]
set_property -dict {PACKAGE_PIN U12  IOSTANDARD LVCMOS33} [get_ports {oled_gpio[3]}]
