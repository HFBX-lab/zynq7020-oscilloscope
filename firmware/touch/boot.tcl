# Called only by the explicitly authorized physical-touch session runner.
if {[catch {
connect -url tcp:127.0.0.1:3121
if {[string first "HelloFpga JTAG-SP2 YOUR_CABLE_SERIAL" [jtag targets]]<0} {error "Expected cable absent"}
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
set boot [mrd -value 0xF800025C]
if {($boot & 7)!=0} {error "JTAG boot mode required"}
puts "BOOT_MODE=$boot"
targets -set -filter {name == "APU"}
rst -system -stop
targets -set -filter {name == "xc7z020"}
fpga -file {@RUN@/platform.bit}
if {[fpga -state] ne "FPGA is configured"} {error "PL configuration failed"}
targets -set -filter {name == "ARM Cortex-A9 MPCore #0"}
rst -processor -clear-registers
source {@RUN@/ps7_init.tcl}
source {@RUN@/board_init_helpers.tcl}
sp2_init_ps_without_ddr
sp2_init_ddr_and_finish
ps7_post_config
dow {@RUN@/application.elf}
con
puts TOUCH_APP_RAM_LOADED
disconnect
} e opts]} {puts stderr "BOOT_ERROR: $e";puts stderr [dict get $opts -errorinfo];exit 1}
exit
