# Dedicated50M-parameter OOC netlist for the new RAW path; no hardware access.
set src [file dirname [file normalize [info script]]]
create_project -in_memory -part xc7z020clg484-1
read_verilog [list [file join $src adc_cic_decimator.v] [file join $src adc_trigger_ring_core.v] [file join $src adc_live_axi.v]]
synth_design -top adc_live_axi -part xc7z020clg484-1 -mode out_of_context -generic {CONVERSION_HZ=50000000}
create_clock -name ctrl -period 10 [get_ports s_axi_aclk]
create_clock -name sample -period 20 [get_ports sample_clk]
set_clock_groups -asynchronous -group [get_clocks ctrl] -group [get_clocks sample]
report_utilization -file utilization.rpt
write_checkpoint -force raw50_axi_ooc.dcp
write_verilog -force -mode funcsim raw50_axi_funcsim.v
puts "RAW50_SYNTH_PASS: actual50M parameter"
exit
