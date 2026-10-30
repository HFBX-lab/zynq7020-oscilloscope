# Offline source constraint check against the already synthesized candidate.
open_project [lindex $argv 0]
open_run synth_1
source [lindex $argv 1]
puts "ADC50_CONSTRAINT_PROBE_PASS: current CDC/reset/held payload/IOB constraints applied"
exit
