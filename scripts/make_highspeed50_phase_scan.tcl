# DIAGNOSTIC ONLY: scan bus eye without pretending the old delay hypothesis fits.
# Input timing failures remain in reports; only internal FF timing gates loading.
set src [lindex $argv 0]
set out [lindex $argv 1]
file mkdir $out
open_checkpoint $src
set mmcm [get_cells -hier -filter {NAME =~ */adc_front/inst/adc_mmcm}]
if {[llength $mmcm]!=1 || [get_property CLKOUT1_PHASE $mmcm]!=15.0} {error "Expected source15deg"}
set fd [open [file join $out scan.tsv] w]
puts $fd "phase\tinternal_wns\tinternal_whs\tinput_setup_slack\tinput_hold_slack"
foreach phase {90 180 270} {
 set_property CLKOUT1_PHASE $phase $mmcm
 set dir [file join $out phase$phase];file mkdir $dir
 foreach type {max min} {
  set path [get_timing_paths -from [all_registers] -to [all_registers] -delay_type $type -max_paths 1]
  if {[llength $path]!=1 || [get_property SLACK $path]<0} {error "Diagnostic phase internal timing failed"}
  set internal_$type [get_property SLACK $path]
 }
 foreach {kind type} {setup max hold min} {
  set paths [get_timing_paths -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type $type -max_paths 22]
  if {[llength $paths]!=22} {error "Diagnostic phase input paths absent"}
  set input_$kind [get_property SLACK [lindex $paths 0]]
  report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -delay_type $type -max_paths 22 -file [file join $dir adc_input_${kind}.rpt]
 }
 report_timing_summary -file [file join $dir timing_summary.rpt]
 report_property -all $mmcm -file [file join $dir mmcm.rpt]
 write_bitstream -force [file join $dir scan.bit]
 puts $fd "$phase\t$internal_max\t$internal_min\t$input_setup\t$input_hold";flush $fd
}
close $fd
puts "DIAGNOSTIC_PHASE_SCAN_GENERATED: notdelivery, inputhypothesisfailuresretained, internalFFtimingpositive"
exit
