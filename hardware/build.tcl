# Run from build/vivado, never connects to a hardware server.
set script_dir [file dirname [file normalize [info script]]]
set root [file normalize [file join $script_dir ..]]
set mode [lindex $argv 0]
if {$mode eq ""} {set mode 0}
if {$mode ni {0 1 2}} {error "Pattern must be 0, 1, or 2"}
set variant [lindex {colorbars grid waveform} $mode]
set parts [get_parts xc7z020clg484-1]
puts "VERIFIED_DEVICE_SUPPORT=$parts"
if {[llength $parts]!=1} {error "Required Zynq part support unavailable"}
create_project -in_memory -part xc7z020clg484-1
read_verilog [glob [file join $script_dir rtl *.v]]
read_xdc [list [file join $script_dir constraints sp2_v13b_j35.xdc]]
synth_design -top lcd_top -part xc7z020clg484-1 -generic [list PATTERN_MODE=$mode]
foreach port [get_ports] {
    if {[get_property PACKAGE_PIN $port] eq ""} {error "Missing package pin: $port"}
}
report_io -file pin_assignments.rpt
write_checkpoint -force post_synth.dcp
report_utilization -file utilization_synth.rpt
opt_design
place_design
route_design
write_checkpoint -force post_route.dcp
report_timing_summary -file timing_summary.rpt
report_drc -file drc.rpt
report_utilization -file utilization_route.rpt
set setup_paths [get_timing_paths -delay_type max -max_paths 1]
set hold_paths [get_timing_paths -delay_type min -max_paths 1]
if {[llength $setup_paths] && [get_property SLACK $setup_paths]<0} {error "Setup timing failed"}
if {[llength $hold_paths] && [get_property SLACK $hold_paths]<0} {error "Hold timing failed"}
write_bitstream -force lcd_${variant}.bit
puts "BUILD_PASS: $variant offline bitstream generated; not downloaded or panel-tested."
exit
