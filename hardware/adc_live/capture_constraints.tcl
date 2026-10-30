set here [file dirname [file normalize [info script]]]
source [file join $here .. touch_soc timing_constraints.tcl]
set first {}
foreach c [get_cells -hier -filter {NAME =~ *adc_capture* && ASYNC_REG == TRUE}] {
 if {[regexp {(request_sync|complete_sync|error_sync|enable_sync|ready_sync|config_req_sync|config_ack_sync|wait_sync|progress_sync)_reg\[0\]$} [get_property NAME $c]]} {
  set p [get_pins -of_objects $c -filter {REF_PIN_NAME == D}]
  if {[llength $p]!=1} {error "Missing ADC CDC D pin"}
  lappend first {*}$p
 }
}
if {[llength $first]!=9} {error "Expected nine ADC first-stage crossings, found $first"}
foreach p $first {
 set starts [all_fanin -flat -startpoints_only -to $p]
 if {![llength $starts]} {error "Missing ADC crossing source $p"}
 set_max_delay -datapath_only 10.0 -from $starts -to $p
}
# The three-bit request payload is held throughout req/ack. The destination
# samples it one extra sample clock after the synchronized request arrives.
set payload_from [get_cells -hier -filter {NAME =~ */adc_capture/inst/rate_request_reg* && REF_NAME =~ FD*}]
set payload_to [get_cells -hier -filter {NAME =~ */adc_capture/inst/rate_sample_reg* && REF_NAME =~ FD*}]
if {[llength $payload_from]!=3 || [llength $payload_to]!=3} {error "Missing rate configuration bundled payload"}
set_max_delay -datapath_only 40.0 -from $payload_from -to $payload_to
# Trigger config is held by AXI ownership before ARM until done/abort.
set trigger_from [get_cells -hier -filter {NAME =~ */adc_capture/inst/cfg_trigger*_reg* && REF_NAME =~ FD*}]
# The same held payload also feeds validation at request acceptance. Bound its
# state/error consumers as well, rather than timing this bundled bus as a live
# unrelated-clock signal. AXI refuses config writes while the request is owned.
set trigger_to [get_cells -hier -filter {(NAME =~ */adc_capture/inst/snapshot/active*_reg* || NAME =~ */adc_capture/inst/snapshot/state_reg* || NAME =~ */adc_capture/inst/snapshot/result_error_reg*) && REF_NAME =~ FD*}]
if {![llength $trigger_from] || ![llength $trigger_to]} {error "Missing held trigger config payload"}
set_max_delay -datapath_only 40.0 -from $trigger_from -to $trigger_to
# Frozen metadata is established one sample cycle before completion toggles;
# the control domain adds synchronized completion and one extra settle cycle.
set frozen_from [get_cells -hier -filter {NAME =~ */adc_capture/inst/snapshot/frozen*_s_reg* && REF_NAME =~ FD*}]
set frozen_to {}
foreach n {matched auto_forced trigger_after record_start samples_seen} {
 lappend frozen_to {*}[get_cells -hier -filter "NAME =~ */adc_capture/inst/snapshot/${n}_reg* && REF_NAME =~ FD*"]
}
if {![llength $frozen_from] || ![llength $frozen_to]} {error "Missing frozen trigger metadata payload"}
# 50MHz: frozen payload precedes completion by20ns; bound it to20ns.
set_max_delay -datapath_only 20.0 -from $frozen_from -to $frozen_to
set releases {}
foreach c [get_cells -hier -filter {ASYNC_REG == TRUE}] {
 set n [get_property NAME $c]
 if {([string match *adc_capture* $n] && [regexp {(pattern_release|ctrl_release|sample_release)_reg\[[01]\]$} $n]) || ([string match *adc_front* $n] && [regexp {(release_sync|drive_release_sync)_reg\[[01]\]$} $n])} {
  set p [get_pins -of_objects $c -filter {REF_PIN_NAME == CLR}]
  if {[llength $p]!=1} {error "Missing ADC reset synchronizer CLR $c"}
  lappend releases {*}$p
 }
}
if {[llength $releases]!=10} {error "Expected ten ADC reset-release flops: $releases"}
set_false_path -to $releases
set io_regs [get_cells -hier -filter {NAME =~ */adc_front/inst/captured_reg* && REF_NAME =~ FD*}]
if {[llength $io_regs]!=22} {error "Expected 22 parallel input capture flops"}
report_timing -from [get_ports {ad_data_1[*] ad_data_2[*] ad_otr_1 ad_otr_2}] -max_paths 22 -file adc_input_setup.rpt
