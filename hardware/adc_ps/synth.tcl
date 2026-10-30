set src [file dirname [file normalize [info script]]]
create_project -in_memory -part xc7z020clg484-1
read_verilog [list [file join $src adc_snapshot_core.v] [file join $src rtl adc_axi_capture.v]]
synth_design -top adc_axi_capture -part xc7z020clg484-1 -mode out_of_context
create_clock -name ctrl -period 10 [get_ports s_axi_aclk]
create_clock -name sample -period 20 [get_ports sample_clk]
set_clock_groups -asynchronous -group [get_clocks ctrl] -group [get_clocks sample]
report_utilization -file utilization.rpt
report_cdc -file cdc.rpt
write_checkpoint -force adc_axi_ooc.dcp
write_verilog -force -mode funcsim adc_axi_funcsim.v
puts "AXI_SYNTH_PASS: internal digital AXI capture; no hardware execution"
exit
