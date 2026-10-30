set soc_here [file dirname [file normalize [info script]]]
# Reuse the known-good screen platform source without editing it or its outputs.
source [file join $soc_here .. ps create_system.tcl]
add_files [list [file join $soc_here .. adc rtl adc_snapshot_core.v] [file join $soc_here .. adc_ps rtl adc_axi_capture.v]]
update_compile_order -fileset sources_1
set capture [create_bd_cell -type module -reference adc_axi_capture adc_capture]
set_property CONFIG.NUM_MI {4} [get_bd_cells ctrl_ic]
connect_bd_intf_net [get_bd_intf_pins ctrl_ic/M03_AXI] [get_bd_intf_pins adc_capture/S_AXI]
connect_bd_net [get_bd_pins ps7/FCLK_CLK0] [get_bd_pins adc_capture/s_axi_aclk] [get_bd_pins ctrl_ic/M03_ACLK]
connect_bd_net [get_bd_ports clk_50] [get_bd_pins adc_capture/sample_clk]
connect_bd_net [get_bd_pins rst_axi/peripheral_aresetn] [get_bd_pins adc_capture/s_axi_aresetn]
connect_bd_net [get_bd_pins rst_axi/interconnect_aresetn] [get_bd_pins ctrl_ic/M03_ARESETN]
# FCLK derives from the exact 33.333MHz PS reference: metadata is 99999001Hz,
# not the rounded 100MHz label used by the standalone RTL/OOC clock contract.
set ctrl_hz [get_property CONFIG.FREQ_HZ [get_bd_pins ps7/FCLK_CLK0]]
set_property CONFIG.FREQ_HZ $ctrl_hz [get_bd_intf_pins adc_capture/S_AXI]
assign_bd_address
set new_segments [get_bd_addr_segs -quiet ps7/Data/SEG_adc_capture*]
if {[llength $new_segments]!=1} {error "Expected one ADC AXI segment: $new_segments"}
set_property range 64K $new_segments
set_property offset 0x43C10000 $new_segments
validate_bd_design
set fd [open adc_address_map.txt w]
foreach seg [get_bd_addr_segs -of_objects [get_bd_addr_spaces ps7/Data]] {
    puts $fd "$seg OFFSET=[get_property OFFSET $seg] RANGE=[get_property RANGE $seg]"
}
close $fd
save_bd_design
write_bd_tcl -force adc_system_recreate.tcl
generate_target all [get_files screen_system.bd]
make_wrapper -files [get_files screen_system.bd] -top
update_compile_order -fileset sources_1
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $soc_here capture_constraints.tcl] [get_runs impl_1]
puts "ADC_SOC_BD_PASS: 43C10000 internal digital test; existing display addresses retained"
