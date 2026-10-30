# A full Tcl hook is required: Vivado's XDC parser rejects control-flow commands.
set first [get_pins -hier -filter {NAME =~ */touch_cdc/inst/stage1_reg*/D}]
if {[llength $first]!=4} {error "Expected four touch input first-stage D pins: $first"}
set_false_path -from [get_ports {tp_sda tp_scl tp_int}] -to $first
# Repair the preserved baseline's missing -from max delay in this candidate only.
set display_first [get_pins -hier -filter {NAME =~ */status_cdc/inst/stage1_reg*/D}]
if {[llength $display_first]!=4} {error "Expected four video status first-stage D pins"}
foreach p $display_first {
 set starts [all_fanin -flat -startpoints_only -to $p]
 if {[llength $starts]!=1} {error "Unexpected video diagnostic source: $p"}
 if {[get_property REF_PIN_NAME $starts] eq "LOCKED"} {set_false_path -to $p} else {set_max_delay -datapath_only 10.0 -from $starts -to $p}
}
