# Offline stubs. There are deliberately no mrd/mwr/connect/targets commands here.
set PCW_SILICON_VER_1_0 0
set PCW_SILICON_VER_2_0 1
set PCW_SILICON_VER_3_0 2
set revision 0
set calls {}
proc ps_version {} {return $::revision}
proc mask_poll {args} {return}
proc mrd {args} {return 1}
foreach suffix {1_0 2_0 3_0} {
    foreach phase {mio pll clock ddr peripherals} {
        proc ps7_${phase}_init_data_${suffix} {} [format {lappend ::calls %s_%s} $phase $suffix]
    }
}
source [file join [file dirname [info script]] .. firmware board_init_helpers.tcl]
if {[llength $calls]} {error "Sourcing helpers performed board actions"}
foreach rev {0 1 2 3} suffix {1_0 2_0 3_0 3_0} {
    set revision $rev;set calls {}
    sp2_init_ps_without_ddr
    # peripherals enables DDR IOB power saving: forbidden before DDR training.
    set expected [list mio_$suffix pll_$suffix clock_$suffix]
    if {$calls ne $expected} {error "Unexpected non-DDR stages: $calls"}
    set calls {}
    sp2_init_ddr_and_finish
    if {$calls ne [list ddr_$suffix peripherals_$suffix]} {error "Unexpected DDR stage: $calls"}
}
set revision 99;set calls {}
if {![catch {sp2_init_ps_without_ddr}]} {error "Unknown silicon revision accepted"}
if {[llength $calls]} {error "Unknown silicon revision wrote registers"}
if {![catch {sp2_init_ddr_and_finish}]} {error "Unknown silicon revision accepted in DDR stage"}
set revision 3;set calls {}
proc ps7_ddr_init_data_3_0 {} {lappend ::calls ddr_3_0;error "Simulated training failure"}
if {![catch {sp2_init_ddr_and_finish}]} {error "DDR error ignored"}
if {$calls ne {ddr_3_0}} {error "Peripherals ran after failed DDR initialization"}
set calls {};set vendor_timeout_returned 0
# Real generated mask_poll prints then returns on timeout; it does not throw.
proc mask_poll {addr mask} {set ::vendor_timeout_returned 1;return}
proc mrd {args} {return 0}
proc ps7_ddr_init_data_3_0 {} {lappend ::calls ddr_3_0;mask_poll 0xF8006054 7}
set original_poll [info body mask_poll]
if {![catch {sp2_init_ddr_and_finish} e]} {error "Non-throwing vendor timeout accepted"}
if {$calls ne {ddr_3_0}} {error "Peripherals ran after poll timeout"}
if {$vendor_timeout_returned} {error "Unsafe vendor timeout implementation was used"}
if {[info body mask_poll] ne $original_poll} {error "Vendor poll not restored after timeout"}
proc mrd {args} {return 1}
set calls {}
sp2_init_ddr_and_finish
if {$calls ne {ddr_3_0 peripherals_3_0}} {error "Successful poll lost stage ordering"}
if {[info body mask_poll] ne $original_poll} {error "Vendor poll not restored after success"}
proc mrd {args} {return 3}
set calls {}
if {![catch {sp2_init_ddr_and_finish}]} {error "Self-refresh accepted as Normal mode"}
if {$calls ne {ddr_3_0}} {error "Peripherals ran outside Normal mode"}
if {[info body mask_poll] ne $original_poll} {error "Vendor poll not restored after mode failure"}
puts "PASS: no source actions; silicon 1.0/2.0/3.0/3.1; DDR before peripherals; strict timeout/Normal mode; errors stop and restore poll"
exit
