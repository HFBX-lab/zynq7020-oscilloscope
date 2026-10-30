# Offline diagnostic only; never connects to hardware or writes a bitstream.
open_checkpoint [lindex $argv 0]
set out [lindex $argv 1]
set adc [get_clocks {adc_clock1 adc_clock2}]
set raw [get_clocks raw]
report_property -all [get_cells -hier -filter {NAME =~ */adc_front/inst/adc_mmcm}] -file [file join $out mmcm-properties.rpt]
foreach variant {none start0 end0 end1} {
 set_multicycle_path 2 -setup -reset_path -end -from $adc -to $raw
 if {$variant eq "start0"} {set_multicycle_path 0 -hold -start -from $adc -to $raw}
 if {$variant eq "end0"} {set_multicycle_path 0 -hold -end -from $adc -to $raw}
 if {$variant eq "end1"} {set_multicycle_path 1 -hold -end -from $adc -to $raw}
 report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type min -max_paths 1 -file [file join $out hold-$variant.rpt]
 report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type max -max_paths 1 -file [file join $out setup-$variant.rpt]
}
puts TIMING_PROBE_COMPLETE
exit
