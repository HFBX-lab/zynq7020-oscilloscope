# Full Tcl implementation PRE hook (XDC's restricted parser cannot run loops).
# Bound only the first data synchronizer stage. The second stage remains fully
# timed; do not broadly cut FCLK0 versus clk_50 because VDMA also uses them.
set adc_first {}
foreach cell [get_cells -hier -filter {NAME =~ *adc_capture* && ASYNC_REG == TRUE}] {
    if {[regexp {(request_sync|complete_sync|error_sync|enable_sync)_reg\[0\]$} [get_property NAME $cell]]} {
        set p [get_pins -of_objects $cell -filter {REF_PIN_NAME == D}]
        if {[llength $p]!=1} {error "ADC synchronizer has no D pin: $cell"}
        lappend adc_first {*}$p
    }
}
if {[llength $adc_first]!=4} {error "Expected four ADC first-stage data synchronizers, found $adc_first"}
foreach p $adc_first {
    set starts [all_fanin -flat -startpoints_only -to $p]
    if {[llength $starts]==0} {error "ADC synchronizer source missing: $p"}
    set_max_delay -datapath_only 10.0 -from $starts -to $p
}
# Only the reset-release synchronizer's async assertion pins are exempt. Its
# synchronous stage0->stage1 release path stays timed with its local clock.
set adc_release {}
foreach cell [get_cells -hier -filter {NAME =~ *adc_capture* && ASYNC_REG == TRUE}] {
    if {[regexp {(pattern_release|ctrl_release|sample_release)_reg\[[01]\]$} [get_property NAME $cell]]} {
        set p [get_pins -of_objects $cell -filter {REF_PIN_NAME == CLR}]
        if {[llength $p]!=1} {error "ADC reset-release flop has no CLR: $cell"}
        lappend adc_release {*}$p
    }
}
if {[llength $adc_release]!=6} {error "Expected six ADC reset-release flops"}
set_false_path -to $adc_release

# The preserved baseline panel.xdc uses a max-delay without -from. 2020.2
# rejects that datapath-only exception (18-540). Add valid, explicit source
# startpoints in THIS candidate only; do not edit the verified baseline files.
set display_first [get_pins -hier -filter {NAME =~ */status_cdc/inst/stage1_reg*/D}]
if {[llength $display_first]!=4} {error "Expected four existing display status synchronizer inputs"}
foreach p $display_first {
    set starts [all_fanin -flat -startpoints_only -to $p]
    if {[llength $starts]!=1} {error "Unexpected status sources for $p: $starts"}
    if {[get_property REF_PIN_NAME $starts] eq "LOCKED"} {
        # MMCM LOCKED is an asynchronous level, not a register clock startpoint.
        # Cut only its FIRST diagnostic synchronizer input, avoiding path
        # segmentation. Stage1->stage2 is still fully timed and ASYNC_REG.
        set_false_path -to $p
    } else {
        set_max_delay -datapath_only 10.0 -from $starts -to $p
    }
}
