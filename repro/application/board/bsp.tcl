# Offline platform/BSP generation only. Never connects to hardware.
if {$argc!=3} {error "Usage: bsp.tcl XSA WORKSPACE standalone|freertos"}
set xsa [file normalize [lindex $argv 0]]
set workspace [file normalize [lindex $argv 1]]
set mode [lindex $argv 2]
if {$mode ni {standalone freertos}} {error "Unknown OS"}
set os [expr {$mode eq "freertos" ? "freertos10_xilinx" : "standalone"}]
setws $workspace
platform create -name sp2_platform -hw $xsa -proc ps7_cortexa9_0 -os $os -no-boot-bsp
platform active sp2_platform
domain active ${os}_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
if {$mode eq "freertos"} {
    bsp config tick_rate 1000
    bsp config total_heap_size 1048576
    bsp config check_for_stack_overflow 2
    bsp config use_task_fpu_support 2
    bsp config use_newlib_reent true
    bsp config use_malloc_failed_hook true
    bsp config use_freertos_asserts true
    bsp config use_trace_facility true
    bsp config minimal_stack_size 1024
    bsp config timer_task_stack_depth 1024
}
platform write
platform generate
puts "LVGL_BSP_PASS: $mode"
exit
