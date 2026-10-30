set_property -dict {PACKAGE_PIN AB1 IOSTANDARD LVCMOS33 DRIVE 4 SLEW SLOW} [get_ports tp_sda]
set_property -dict {PACKAGE_PIN U5 IOSTANDARD LVCMOS33 DRIVE 4 SLEW SLOW} [get_ports tp_scl]
set_property -dict {PACKAGE_PIN U4 IOSTANDARD LVCMOS33 DRIVE 4 SLEW SLOW} [get_ports tp_int]
# U6 reset mapping remains in the preserved panel.xdc. V5 (NC) is not allocated.
set_false_path -to [get_ports {tp_sda tp_scl tp_int}]
