# Offline platform/BSP generation only. Never connects to hardware.
if {$argc != 2} {error "Usage: bsp_storage.tcl XSA WORKSPACE"}
setws [lindex $argv 1]
platform create -name sp2_storage -hw [lindex $argv 0] -proc ps7_cortexa9_0 -os freertos10_xilinx -no-boot-bsp
platform active sp2_storage
domain active freertos10_xilinx_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
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
bsp setlib -name xilffs -ver 4.4
bsp config read_only false
bsp config use_mkfs false
bsp config enable_exfat false
platform write
platform generate
puts M5_STORAGE_BSP_PASS
exit
