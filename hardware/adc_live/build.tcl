source [file join [file dirname [file normalize [info script]]] create_system.tcl]
set_property strategy Flow_PerfOptimized_high [get_runs synth_1]
launch_runs synth_1 -jobs 4
wait_on_run synth_1
if {[get_property PROGRESS [get_runs synth_1]] ne "100%"} {error "ADC Live SoC synthesis failed"}
launch_runs impl_1 -to_step route_design -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "ADC Live SoC implementation failed"}
open_run impl_1
report_timing_summary -file timing_summary.rpt
report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type max -max_paths 22 -file adc_input_setup.rpt
report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type min -max_paths 22 -file adc_input_hold.rpt
# A green global summary must not hide an incorrectly shifted hold edge.
if {abs([get_property PERIOD [get_clocks raw]]-40.0)<.001} {
 foreach {kind required} {setup 45.000 hold 5.000} {
  set fd [open adc_input_${kind}.rpt r];set timing_text [read $fd];close $fd
  if {[regexp -all "Requirement: +${required}ns" $timing_text]!=22} {error "25M input $kind uses wrong conversion edge"}
 }
 puts "ADC25_EDGE_AUDIT_PASS:22 setup45ns/hold5ns requirements"
}
if {abs([get_property PERIOD [get_clocks raw]]-20.0)<.001} {
 foreach {kind required} {setup 29.167 hold 9.167} {
  set fd [open adc_input_${kind}.rpt r];set timing_text [read $fd];close $fd
  if {[regexp -all "Requirement: +${required}ns" $timing_text]!=22} {error "50M input $kind uses wrong conversion edge"}
 }
 puts "ADC50_EDGE_AUDIT_PASS:22 setup29.167ns/hold9.167ns requirements"
}
report_pulse_width -all_violators -file pulse_width_violations.rpt
set fd [open pulse_width_violations.rpt r];set pw_text [read $fd];close $fd
# A non-empty violations table (Slack heading) is a build failure too.
if {[regexp -nocase {slack|violated} $pw_text]} {error "Internal pulse width timing failed"}
report_pulse_width -high_pulse -low_pulse -limit 30 -file pulse_width.rpt
report_datasheet -file output_datasheet.rpt
report_utilization -file utilization.rpt
report_drc -file drc.rpt
report_cdc -file cdc.rpt
report_io -file pin_assignments.rpt
set setup [get_timing_paths -delay_type max -max_paths 1]
set hold [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup] && [get_property SLACK $setup]<0} {error "ADC Live SoC setup timing failed"}
if {[llength $hold] && [get_property SLACK $hold]<0} {error "ADC Live SoC hold timing failed"}
close_design
launch_runs impl_1 -to_step write_bitstream -jobs 4
wait_on_run impl_1
if {[get_property PROGRESS [get_runs impl_1]] ne "100%"} {error "ADC Live SoC bitstream failed"}
open_run impl_1
file copy -force [file join [get_property DIRECTORY [get_runs impl_1]] scope_live_top.bit] sp2_adc_live.bit
write_hw_platform -fixed -include_bit -force -file sp2_adc_live.xsa
write_checkpoint -force sp2_adc_live_routed.dcp
puts "ADC_LIVE_BUILD_PASS: physical ADC candidate; NOT downloaded or run"
exit
