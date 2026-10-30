# Offline only: no connect, reset, FPGA load, or target access.
set root {D:/FPGA_Project/SP2_Storage_Build/hardware}
set baseline {D:/FPGA_Project/SP2_Boot_Build/fsbl/hardware}
if {[file exists $root/storage.xsa]} {error "Refusing to overwrite storage.xsa"}
create_project sp2_storage_ps $root/project -part xc7z020clg484-1
set_property target_language Verilog [current_project]
foreach relative {adc/rtl/adc_snapshot_core.v adc_live/adc_cic_decimator.v adc_live/adc_frontend.v adc_live/adc_live_axi.v adc_live/adc_trigger_ring_core.v ps/screen_rgb_io.v ps/status_sync.v rtl/lcd_powerup.v touch_soc/touch_pads.v} {
    add_files -norecurse $baseline/source/$relative
}
set dst $root/project/sp2_storage_ps.srcs/sources_1/bd/screen_system/screen_system.bd
file mkdir [file dirname $dst]
file copy $baseline/input/screen_system.bd $dst
add_files -norecurse $dst
set bd [get_files screen_system.bd]
open_bd_design $bd
set ps [get_bd_cells ps7]
set_property -dict [list CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1} CONFIG.PCW_QSPI_GRP_SINGLE_SS_ENABLE {1} CONFIG.PCW_QSPI_QSPI_IO {MIO 1 .. 6}] $ps
report_property -all $ps -file $root/ps-before-properties.rpt
set_property -dict [list CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} CONFIG.PCW_SD0_GRP_CD_ENABLE {1} CONFIG.PCW_SD0_GRP_CD_IO {MIO 47} CONFIG.PCW_SD0_GRP_WP_ENABLE {0} CONFIG.PCW_SD0_GRP_POW_ENABLE {0}] $ps
report_property -all $ps -file $root/ps-after-properties.rpt
validate_bd_design
save_bd_design
generate_target all $bd
set wrapper [make_wrapper -files $bd -top]
add_files -norecurse $wrapper
set_property top screen_system_wrapper [current_fileset]
update_compile_order -fileset sources_1
write_hw_platform -fixed -file $root/storage.xsa
puts M5_STORAGE_METADATA_PASS
close_project
exit
