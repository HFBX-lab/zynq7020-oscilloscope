# Offline platform/BSP generation only. Never connects to hardware.
if {$argc != 2} {error "Usage: bsp.tcl XSA WORKSPACE"}
set xsa [file normalize [lindex $argv 0]]
set workspace [file normalize [lindex $argv 1]]
setws $workspace
platform create -name adc_platform -hw $xsa -proc ps7_cortexa9_0 -os standalone -no-boot-bsp
platform active adc_platform
domain active standalone_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
platform write
platform generate
puts "ADC_BSP_PASS: standalone internal digital candidate"
exit
