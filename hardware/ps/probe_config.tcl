create_project -in_memory -part xc7z020clg484-1
create_bd_design probe
set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps]
foreach part {{MT41K256M16 TW-107} {MT41K256M16 RE-125} {Custom}} {
 if {[catch {set_property CONFIG.PCW_UIPARAM_DDR_PARTNO $part $ps} message]} {
  puts "PART_REJECTED=$part : $message"
 } else {
  puts "PART_ACCEPTED=[get_property CONFIG.PCW_UIPARAM_DDR_PARTNO $ps]"
  foreach p [lsort [list_property $ps]] {
   if {[string match CONFIG.PCW_UIPARAM_DDR_* $p]} {puts "$p=[get_property $p $ps]"}
  }
 }
}
set_property -dict [list CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_USE_S_AXI_HP0 {1} \
 CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1} CONFIG.PCW_UART1_UART1_IO {MIO 52 .. 53} \
 CONFIG.PCW_CRYSTAL_PERIPHERAL_FREQMHZ {33.333} CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100}] $ps
puts "UART_CHECK=[get_property CONFIG.PCW_UART1_UART1_IO $ps]"
foreach {name vlnv} {vtc xilinx.com:ip:v_tc:6.2 video xilinx.com:ip:v_axi4s_vid_out:4.0} {
 set c [create_bd_cell -type ip -vlnv $vlnv $name]
 puts "ALL_CONFIG=$name"
 foreach p [lsort [list_property $c]] {if {[string match CONFIG.* $p]} {puts "$p=[get_property $p $c]"}}
}
exit
