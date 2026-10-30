# XSCT 2020.2, entirely offline. NO connect/targets/dow/con/fpga commands.
if {$argc!=3} {error "Usage: xsct build.tcl XSA SOURCE_DIR WORKSPACE"}
set xsa [file normalize [lindex $argv 0]]
set source_dir [file normalize [lindex $argv 1]]
set workspace [file normalize [lindex $argv 2]]
setws $workspace
platform create -name sp2_platform -hw $xsa -proc ps7_cortexa9_0 -os standalone -no-boot-bsp
platform active sp2_platform
domain active standalone_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
platform write
platform generate
puts "BSP_BUILD_PASS: standalone platform generated; application compilation is performed by the same Vitis ARM GCC toolchain"
exit
