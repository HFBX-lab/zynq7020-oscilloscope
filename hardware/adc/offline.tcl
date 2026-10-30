# This script only synthesizes a standalone digital core and checks package data.
# No hardware server, JTAG, programming or bitstream commands.
set source_dir [file dirname [file normalize [info script]]]
create_project -in_memory -part xc7z020clg484-1
read_verilog [file join $source_dir rtl adc_snapshot_core.v]
synth_design -top adc_snapshot_core -part xc7z020clg484-1 -mode out_of_context
create_clock -name ctrl -period 10 [get_ports ctrl_clk]
create_clock -name sample -period 20 [get_ports sample_clk]
# Domains are unrelated. Toggle/error synchronizers and frozen dual-port RAM
# ownership are the explicit CDC protocol, examined in simulation and reports.
set_clock_groups -asynchronous -group [get_clocks ctrl] -group [get_clocks sample]
report_utilization -file utilization.rpt
report_timing_summary -file timing_summary.rpt
report_cdc -file cdc.rpt
set block36 [get_cells -hier -filter {REF_NAME == RAMB36E1}]
set block18 [get_cells -hier -filter {REF_NAME == RAMB18E1}]
set capacity_bits [expr {36864*[llength $block36]+18432*[llength $block18]}]
# Each 32-bit word has 10 constant-zero padding bits. Synthesis may remove them;
# the RAM must hold 4096*(2*10 data + 2 OTR) real bits, not 4096*32 variable bits.
if {$capacity_bits<4096*22} {error "Insufficient inferred BRAM capacity for 4096 dual ADC samples"}
write_checkpoint -force adc_snapshot_ooc.dcp
write_verilog -force -mode funcsim adc_snapshot_funcsim.v

set fd [open package-pins.csv w]
puts $fd "bank,port,package_pin,pin_func"
foreach bank {33 35} {
    set checked_ports {}
    set path [file join $source_dir constraints adc_bank${bank}.xdc]
    set f [open $path r]
    set text [read $f];close $f
    foreach line [split $text \n] {
        if {![regexp {PACKAGE_PIN ([A-Z]+[0-9]+)\} \[get_ports \{([^\x7d]+)\}} $line -> ball port]} {continue}
        set p [get_package_pins -quiet $ball]
        if {[llength $p]!=1} {error "Unknown package pin $ball"}
        set actual_bank [get_property BANK $p]
        set func [get_property PIN_FUNC $p]
        if {$actual_bank!=$bank} {error "$port/$ball bank $actual_bank expected $bank"}
        puts $fd "$bank,$port,$ball,$func"
        lappend checked_ports $port
    }
    if {[llength [lsort -unique $checked_ports]]!=26} {error "Expected 26 signals for Bank$bank"}
}
close $fd
report_property [get_package_pins T22] -file corrected-T22.rpt
report_property [get_package_pins T12] -file old-typo-T12.rpt
puts "ADC_OFFLINE_SYNTH_PASS: BRAM36=[llength $block36] BRAM18=[llength $block18]; package Bank33/35 checked; not implemented or board tested"
exit
