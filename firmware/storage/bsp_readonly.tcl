# Offline BSP generation; does not connect to a target.
if {$argc != 2} {error "Usage: bsp_readonly.tcl XSA WORKSPACE"}
setws [lindex $argv 1]
platform create -name sp2_storage_readonly -hw [lindex $argv 0] -proc ps7_cortexa9_0 -os standalone -no-boot-bsp
platform active sp2_storage_readonly
domain active standalone_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
bsp setlib -name xilffs -ver 4.4
bsp config read_only true
bsp config use_mkfs false
bsp config enable_exfat false
platform write
platform generate
puts M5_READONLY_BSP_PASS
exit
