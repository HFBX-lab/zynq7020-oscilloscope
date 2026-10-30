# Offline controlled experiment: same phase/routing, only clock output DRIVE/SLEW.
set source [lindex $argv 0]
set out [lindex $argv 1]
file mkdir $out
open_checkpoint $source
set_property DRIVE 8 [get_ports {ad_clk_1 ad_clk_2}]
set_property SLEW FAST [get_ports {ad_clk_1 ad_clk_2}]
report_timing_summary -file [file join $out timing_summary.rpt]
foreach {kind type} {setup max hold min} {
 set paths [get_timing_paths -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type $type -max_paths 22]
 if {[llength $paths]!=22} {error "Clock experiment missing input paths"}
 foreach p $paths {if {[get_property SLACK $p]<0} {error "Clock experiment input timing failed"}}
 report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type $type -max_paths 22 -file [file join $out adc_input_${kind}.rpt]
}
foreach type {min max} {if {[get_property SLACK [get_timing_paths -delay_type $type -max_paths 1]]<0} {error "Clock experiment global timing failed"}}
report_io -file [file join $out io.rpt]
write_checkpoint -force [file join $out fast8.dcp]
write_bitstream -force [file join $out fast8.bit]
puts "CLOCK_EXPERIMENT_PASS: onlyad_clk1/2 DRIVE8 SLEWFAST, phase15, sameotherlogic"
exit
