source [file join [file dirname [file normalize [info script]]] create_system.tcl]
set_property strategy Flow_PerfOptimized_high [get_runs synth_1]
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "ADC SoC synthesis failed"}
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "ADC SoC implementation failed"}
open_run impl_1
report_timing_summary -file timing_summary.rpt
report_utilization -file utilization.rpt
report_drc -file drc.rpt
report_cdc -file cdc.rpt
report_io -file pin_assignments.rpt
set setup [get_timing_paths -delay_type max -max_paths 1]
set hold [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup] && [get_property SLACK $setup]<0} {error "ADC SoC setup timing failed"}
if {[llength $hold] && [get_property SLACK $hold]<0} {error "ADC SoC hold timing failed"}
close_design
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "ADC SoC bitstream failed"}
open_run impl_1
file copy -force [file join [get_property DIRECTORY [get_runs impl_1]] screen_system_wrapper.bit] sp2_adc_internal.bit
write_hw_platform -fixed -include_bit -force -file sp2_adc_internal.xsa
write_checkpoint -force sp2_adc_internal_routed.dcp
puts "ADC_SOC_BUILD_PASS: internal digital only; NOT downloaded or run"
exit
