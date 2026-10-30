# Procedure definitions only. Sourcing does not connect, reset, program, or access
# hardware. Run these only in a separately user-authorized board-debug session.
# First source the ps7_init.tcl extracted from THIS delivery's final XSA.
proc sp2_silicon_suffix {} {
    foreach name {PCW_SILICON_VER_1_0 PCW_SILICON_VER_2_0 PCW_SILICON_VER_3_0} {
        if {![info exists ::$name]} {error "Source the matching generated ps7_init.tcl first"}
    }
    if {![llength [info commands ps_version]]} {error "Generated ps_version procedure is missing"}
    set revision [ps_version]
    foreach {name suffix} {PCW_SILICON_VER_1_0 1_0 PCW_SILICON_VER_2_0 2_0 PCW_SILICON_VER_3_0 3_0} {
        if {$revision == [set ::$name]} {return $suffix}
    }
    # Vitis 2020.2 zynq_fsbl/src/fsbl.h: SILICON_VERSION_3_1 == 3.
    # Its generated ps7_init dispatches this revision to the 3_0 tables too.
    # Accept this known revision explicitly; keep unknown future values blocked.
    if {$revision == 3} {return 3_0}
    error "Unsupported silicon revision $revision; no initialization attempted"
}
proc sp2_init_ps_without_ddr {} {
    set suffix [sp2_silicon_suffix]
    # UART driver initializes its own registers. Defer the entire peripherals
    # stage: it also enables DDR IOB power saving, forbidden during training
    # (UG585 DDR Controller Initialization / DDRIOB_DATA0 and DIFF0).
    sp2_run_stages $suffix {mio pll clock}
}
proc sp2_init_ddr_and_finish {} {
    set suffix [sp2_silicon_suffix]
    sp2_run_stages $suffix {ddr peripherals}
}
proc sp2_strict_mask_poll {addr mask} {
    # The generated 2020.2 poll merely prints on timeout. Enforce a finite
    # deadline and propagate both timeout and debugger read errors instead.
    set deadline [expr {[clock milliseconds]+5000}]
    for {set reads 0} {$reads < 1000} {incr reads} {
        set value [mrd -value $addr]
        if {($value & $mask) != 0} {return}
        if {[clock milliseconds] >= $deadline} {break}
        after 5
    }
    error "PS initialization poll timeout: address=$addr mask=$mask last=$value"
}
proc sp2_run_stages {suffix phases} {
    foreach phase $phases {
        set procedure ps7_${phase}_init_data_${suffix}
        if {![llength [info commands $procedure]]} {error "Missing generated procedure $procedure"}
    }
    if {![llength [info commands ::mask_poll]] || [llength [info commands ::sp2_saved_mask_poll]]} {
        error "Missing generated mask_poll or nested initialization attempt"
    }
    # Only this invocation uses the strict poll. Never edit the generated file;
    # restore its command on success or any failure.
    rename ::mask_poll ::sp2_saved_mask_poll
    interp alias {} ::mask_poll {} ::sp2_strict_mask_poll
    set code [catch {
        foreach phase $phases {
            uplevel #0 ps7_${phase}_init_data_${suffix}
            if {$phase eq "ddr"} {
                set status [mrd -value 0xF8006054]
                if {($status & 7) != 1} {error "DDRC did not enter Normal mode: $status"}
            }
        }
    } result options]
    rename ::mask_poll {}
    rename ::sp2_saved_mask_poll ::mask_poll
    return -options $options $result
}
