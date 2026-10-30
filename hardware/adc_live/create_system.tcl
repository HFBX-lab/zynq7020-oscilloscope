set live_here [file dirname [file normalize [info script]]]
source [file join $live_here .. touch_soc create_system.tcl]
add_files [list [file join $live_here adc_cic_decimator.v] [file join $live_here adc_trigger_ring_core.v] [file join $live_here adc_frontend.v] [file join $live_here adc_live_axi.v] [file join $live_here .. adc rtl adc_snapshot_core.v]]
update_compile_order -fileset sources_1
set capture [create_bd_cell -type module -reference adc_live_axi adc_capture]
set_property CONFIG.CONVERSION_HZ 50000000 $capture
set front [create_bd_cell -type module -reference adc_frontend adc_front]
set_property CONFIG.CONVERSION_HZ 50000000 $front
set_property CONFIG.DRIVE_PHASE_DEG 195.0 $front
set_property CONFIG.NUM_MI {5} [get_bd_cells ctrl_ic]
connect_bd_intf_net [get_bd_intf_pins ctrl_ic/M04_AXI] [get_bd_intf_pins adc_capture/S_AXI]
net ps7/FCLK_CLK0 adc_capture/s_axi_aclk ctrl_ic/M04_ACLK
net rst_axi/peripheral_aresetn adc_capture/s_axi_aresetn adc_front/resetn
net rst_axi/interconnect_aresetn ctrl_ic/M04_ARESETN
connect_bd_net [get_bd_ports clk_50] [get_bd_pins adc_front/clk_50]
net adc_front/sample_clk adc_capture/sample_clk
net adc_front/sample_ready adc_capture/input_ready
net adc_front/sample_valid adc_capture/input_valid
foreach {from to} {raw_valid raw_valid raw_data1 raw_data1 raw_data2 raw_data2 raw_otr1 raw_otr1 raw_otr2 raw_otr2} {net adc_front/$from adc_capture/$to}
foreach {from to} {data1 adc_data1 data2 adc_data2 otr1 adc_otr1 otr2 adc_otr2} {net adc_front/$from adc_capture/$to}
foreach {name dir width} {ad_data_1 I 10 ad_data_2 I 10 ad_otr_1 I 1 ad_otr_2 I 1 ad_clk_1 O 1 ad_clk_2 O 1 ad_oe_1 O 1 ad_oe_2 O 1} {
 if {$width==1} {set p [create_bd_port -dir $dir $name]} else {set p [create_bd_port -dir $dir -from [expr {$width-1}] -to 0 $name]}
 connect_bd_net $p [get_bd_pins adc_front/$name]
}
set ctrl_hz [get_property CONFIG.FREQ_HZ [get_bd_pins ps7/FCLK_CLK0]]
set_property CONFIG.FREQ_HZ $ctrl_hz [get_bd_intf_pins adc_capture/S_AXI]
assign_bd_address
set seg [get_bd_addr_segs -quiet ps7/Data/SEG_adc_capture*]
if {[llength $seg]!=1} {error "Missing ADC AXI window"}
set_property range 64K $seg
set_property offset 0x43C10000 $seg
validate_bd_design
save_bd_design
write_bd_tcl -force live_system_recreate.tcl
generate_target all [get_files screen_system.bd]
make_wrapper -files [get_files screen_system.bd] -top
source [file join $live_here make_top.tcl]
add_files -fileset constrs_1 [file join $live_here .. adc constraints adc_bank33.xdc]
add_files -fileset constrs_1 [file join $live_here input_timing.xdc]
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $live_here capture_constraints.tcl] [get_runs impl_1]
puts "ADC_LIVE_BD_PASS: BANK33/J6 50MHz candidate RAW and five filtered record rates; touch retained"
