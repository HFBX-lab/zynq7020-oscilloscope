# Offline generation only: no target connection, reset, download or flash access.
if {$argc != 2} {error "Usage: build_boot_image.tcl XSA WORKSPACE"}
setws [lindex $argv 1]
platform create -name sp2_storage_boot -hw [lindex $argv 0] -proc ps7_cortexa9_0 -os standalone -no-boot-bsp
platform active sp2_storage_boot
domain active standalone_domain
bsp config stdin ps7_uart_1
bsp config stdout ps7_uart_1
bsp setlib -name xilffs -ver 4.4
bsp config read_only true
bsp config use_mkfs false
bsp config enable_exfat false
platform write
platform generate
# Invoke the official template generator directly. Managed `app create` can
# wait indefinitely after its successful template generation on this SDK.
set workspace [lindex $argv 1]
set mss $workspace/sp2_storage_boot/ps7_cortexa9_0/standalone_domain/bsp/system.mss
file mkdir $workspace/sp2_storage_fsbl/src
hsi::utils::generate_app_template -hw [lindex $argv 0] -sw $mss -app zynq_fsbl -processor ps7_cortexa9_0 -os standalone -dir $workspace/sp2_storage_fsbl/src
# The managed app builder is deliberately not invoked. Freeze and build using
# the installed ARM compiler so every actual compiler argument is retained.
puts M5_BOOT_SOURCE_AND_BSP_READY
exit
