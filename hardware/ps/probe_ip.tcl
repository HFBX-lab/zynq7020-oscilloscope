# Read installed IP metadata in a disposable in-memory design; no hardware access.
create_project -in_memory -part xc7z020clg484-1
create_bd_design probe
foreach {name vlnv} {
 ps xilinx.com:ip:processing_system7:5.5
 vdma xilinx.com:ip:axi_vdma:6.3
 vtc xilinx.com:ip:v_tc:6.2
 video xilinx.com:ip:v_axi4s_vid_out:4.0
} {
 set c [create_bd_cell -type ip -vlnv $vlnv $name]
 puts "CELL=$name"
 foreach p [lsort [list_property $c]] {
  if {[string match CONFIG.* $p] && [regexp -nocase {DDR_(PARTNO|BUS_WIDTH|DRAM_WIDTH|DEVICE_CAPACITY|FREQ_MHZ|CL$|CWL$|T_R|T_F|ROW_ADDR|COL_ADDR|BANK_ADDR|DQS|BOARD)|DDR_RAM|CRYSTAL|UART1.*(IO|ENABLE)|BANK[01]_PERIPHERAL|M_AXI_GP0|S_AXI_HP0|FPGA0|MM2S|INCLUDE_S2MM|INCLUDE_SG|NUM_FSTORES|FSYNC|HAS_AXI|GENERATE|GEN_.*(SIZE|POL)|NUM_VIDEO|PIXEL|ASYNC|MASTER|ADDRESS_WIDTH} $p]} {
    puts "$p=[get_property $p $c]"
  }
 }
 puts "PINS=[get_bd_pins $name/*]"
 puts "INTERFACES=[get_bd_intf_pins $name/*]"
}
close_project
exit
