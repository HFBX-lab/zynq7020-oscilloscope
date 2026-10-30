# Offline metadata generation only: no hardware manager/target connection.
if {$argc != 2} {error "usage: -tclargs SOURCE_ROOT OUTPUT_ROOT"}
set src [file normalize [lindex $argv 0]]
set out [file normalize [lindex $argv 1]]
set base $src/repro/hardware/frozen-input
create_project sp2_storage_ps $out/project -part xc7z020clg484-1
set_property target_language Verilog [current_project]
foreach relative {adc/rtl/adc_snapshot_core.v adc_live/adc_cic_decimator.v adc_live/adc_frontend.v adc_live/adc_live_axi.v adc_live/adc_trigger_ring_core.v ps/screen_rgb_io.v ps/status_sync.v rtl/lcd_powerup.v touch_soc/touch_pads.v} {
    add_files -norecurse $base/source/$relative
}
set dst $out/project/sp2_storage_ps.srcs/sources_1/bd/screen_system/screen_system.bd
file mkdir [file dirname $dst]
file copy $src/repro/hardware/original-screen_system.bd $dst
add_files -norecurse $dst
open_bd_design [get_files screen_system.bd]
set ps [get_bd_cells ps7]
set_property -dict [list CONFIG.PCW_QSPI_PERIPHERAL_ENABLE {1} CONFIG.PCW_QSPI_GRP_SINGLE_SS_ENABLE {1} CONFIG.PCW_QSPI_QSPI_IO {MIO 1 .. 6} CONFIG.PCW_SD0_PERIPHERAL_ENABLE {1} CONFIG.PCW_SD0_SD0_IO {MIO 40 .. 45} CONFIG.PCW_SD0_GRP_CD_ENABLE {1} CONFIG.PCW_SD0_GRP_CD_IO {MIO 47} CONFIG.PCW_SD0_GRP_WP_ENABLE {0} CONFIG.PCW_SD0_GRP_POW_ENABLE {0}] $ps
validate_bd_design
save_bd_design
generate_target all [get_files screen_system.bd]
add_files -norecurse [make_wrapper -files [get_files screen_system.bd] -top]
set_property top screen_system_wrapper [current_fileset]
update_compile_order -fileset sources_1
write_hw_platform -fixed -file $out/storage.xsa
close_project
exit
