# Offline routed-design inspection, no hardware manager or JTAG connection.
set routed [lindex $argv 0]
set output [lindex $argv 1]
set expected_conversion [lindex $argv 2]
open_checkpoint $routed
set cells [get_cells -hier -filter {NAME =~ */adc_front/inst/captured_reg*}]
if {[llength $cells]!=22} {error "Expected22 ADC IOB capture registers"}
set fd [open [file join $output capture-placements.csv] w]
foreach c [lsort $cells] {
 puts $fd "[get_property NAME $c],[get_property LOC $c],[get_property BEL $c],[get_property IOB $c]"
}
close $fd
if {$expected_conversion in {10000000 25000000 50000000}} {
 set expected_period [expr {1e9/$expected_conversion}]
 set expected_phase [expr {$expected_conversion==50000000?20.0*195/360:$expected_conversion==25000000?35.0:12.5}]
 set capture_cell [lindex $cells 0]
 set capture_clock [get_clocks -of_objects [get_pins -of_objects $capture_cell -filter {REF_PIN_NAME == C}]]
 set drive_clock [get_clocks -of_objects [get_pins -hier -filter {NAME =~ */adc_front/inst/clk1/C}]]
 if {[llength $capture_clock]!=1 || [llength $drive_clock]!=1} {error "Missing unique capture/drive clocks"}
 foreach c [list $capture_clock $drive_clock] {if {abs([get_property PERIOD $c]-$expected_period)>.001} {error "Unexpected ADC clock period"}}
 set cw [get_property WAVEFORM $capture_clock];set dw [get_property WAVEFORM $drive_clock]
 if {abs([lindex $dw 0]-[lindex $cw 0]-$expected_phase)>.001} {error "Unexpected ADC drive phase"}
 set fd [open [file join $output sampling-clocks.csv] w]
 puts $fd "capture,$capture_clock,[get_property PERIOD $capture_clock],[lindex $cw 0],[lindex $cw 1]"
 puts $fd "drive,$drive_clock,[get_property PERIOD $drive_clock],[lindex $dw 0],[lindex $dw 1]"
 close $fd
 puts "HIGHSPEED_CLOCK_PHASE_PASS:${expected_conversion}Hz period${expected_period}ns drive+${expected_phase}ns"
}
puts "LONG_TIMEBASE_PLACEMENT_PASS:22 ADC input registers"
close_design
exit
