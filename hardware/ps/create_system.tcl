# Local 2020.2 IP versions; no board preset or hardware-server connection.
set here [file dirname [file normalize [info script]]]
set project_dir [file normalize [file join [pwd] project]]
create_project -force sp2_ps_screen $project_dir -part xc7z020clg484-1
set_property target_language Verilog [current_project]
add_files [list [file join $here screen_rgb_io.v] [file join $here status_sync.v] [file join $here .. rtl lcd_powerup.v]]
add_files -fileset constrs_1 [file join $here panel.xdc]
create_bd_design screen_system
proc net {from args} {
    set objects [list [get_bd_pins $from]]
    foreach to $args {lappend objects [get_bd_pins $to]}
    connect_bd_net {*}$objects
}
proc one {path} {connect_bd_net [get_bd_pins const1/dout] [get_bd_pins $path]}
proc zero {path} {connect_bd_net [get_bd_pins const0/dout] [get_bd_pins $path]}
proc optional_zero {path} {if {[llength [get_bd_pins -quiet $path]]} {zero $path}}

set ps [create_bd_cell -type ip -vlnv xilinx.com:ip:processing_system7:5.5 ps7]
# TW-107 is not listed by 2020.2. Same-geometry RE-125 timing profile is a
# conservative candidate, not an assertion of identical package/board delays.
set_property -dict [list CONFIG.PCW_UIPARAM_DDR_PARTNO {MT41K256M16 RE-125} \
    CONFIG.PCW_UIPARAM_DDR_BUS_WIDTH {16 Bit} CONFIG.PCW_UIPARAM_DDR_FREQ_MHZ {400} \
    CONFIG.PCW_CRYSTAL_PERIPHERAL_FREQMHZ {33.333} \
    CONFIG.PCW_PRESET_BANK0_VOLTAGE {LVCMOS 3.3V} CONFIG.PCW_PRESET_BANK1_VOLTAGE {LVCMOS 3.3V} \
    CONFIG.PCW_USE_M_AXI_GP0 {1} CONFIG.PCW_USE_S_AXI_HP0 {1} CONFIG.PCW_S_AXI_HP0_DATA_WIDTH {64} \
    CONFIG.PCW_FPGA0_PERIPHERAL_FREQMHZ {100} \
    CONFIG.PCW_UART1_PERIPHERAL_ENABLE {1} CONFIG.PCW_UART1_UART1_IO {MIO 52 .. 53}] $ps
make_bd_intf_pins_external [get_bd_intf_pins ps7/DDR]
set_property name DDR [get_bd_intf_ports DDR_0]
make_bd_intf_pins_external [get_bd_intf_pins ps7/FIXED_IO]
set_property name FIXED_IO [get_bd_intf_ports FIXED_IO_0]

set vdma [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_vdma:6.3 vdma]
set_property -dict [list CONFIG.c_include_s2mm {0} CONFIG.c_include_sg {0} CONFIG.c_num_fstores {2} \
    CONFIG.c_m_axi_mm2s_data_width {64} CONFIG.c_m_axis_mm2s_tdata_width {24} \
    CONFIG.c_mm2s_genlock_mode {0} CONFIG.c_use_fsync {0} CONFIG.c_mm2s_linebuffer_depth {4096} \
    CONFIG.c_mm2s_max_burst_length {16}] $vdma
set vtc [create_bd_cell -type ip -vlnv xilinx.com:ip:v_tc:6.2 vtc]
set_property -dict [list CONFIG.enable_detection {false} CONFIG.enable_generation {true} \
    CONFIG.HAS_AXI4_LITE {true} CONFIG.VIDEO_MODE {Custom} CONFIG.GEN_HACTIVE_SIZE {1024} \
    CONFIG.GEN_VACTIVE_SIZE {600} CONFIG.GEN_HFRAME_SIZE {1344} \
    CONFIG.GEN_F0_VFRAME_SIZE {635} \
    CONFIG.GEN_HSYNC_START {1184} CONFIG.GEN_HSYNC_END {1204} \
    CONFIG.GEN_F0_VSYNC_VSTART {612} CONFIG.GEN_F0_VSYNC_VEND {615} \
    CONFIG.GEN_F0_VBLANK_HSTART {1024} CONFIG.GEN_F0_VBLANK_HEND {1024} \
    CONFIG.GEN_F0_VSYNC_HSTART {1184} CONFIG.GEN_F0_VSYNC_HEND {1184} \
    CONFIG.GEN_HSYNC_POLARITY {High} CONFIG.GEN_VSYNC_POLARITY {High}] $vtc
set video [create_bd_cell -type ip -vlnv xilinx.com:ip:v_axi4s_vid_out:4.0 video_out]
set_property -dict [list CONFIG.C_S_AXIS_VIDEO_FORMAT {2} CONFIG.C_S_AXIS_VIDEO_DATA_WIDTH {8} \
    CONFIG.C_NATIVE_COMPONENT_WIDTH {8} CONFIG.C_HAS_ASYNC_CLK {0} CONFIG.C_VTG_MASTER_SLAVE {0}] $video
set gpio [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 status_gpio]
set_property -dict [list CONFIG.C_GPIO_WIDTH {4} CONFIG.C_ALL_INPUTS {1} CONFIG.C_IS_DUAL {0}] $gpio
set ctrl [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 ctrl_ic]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {3}] $ctrl
set data [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_interconnect:2.1 data_ic]
set_property -dict [list CONFIG.NUM_SI {1} CONFIG.NUM_MI {1}] $data
foreach name {rst_axi rst_pixel} {
    set r [create_bd_cell -type ip -vlnv xilinx.com:ip:proc_sys_reset:5.0 $name]
}
foreach {name value} {const0 0 const1 1} {
    set c [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconstant:1.1 $name]
    set_property -dict [list CONFIG.CONST_WIDTH {1} CONFIG.CONST_VAL $value] $c
}
set rgb [create_bd_cell -type module -reference screen_rgb_io rgb_io]
set sync [create_bd_cell -type module -reference status_sync status_cdc]
set concat [create_bd_cell -type ip -vlnv xilinx.com:ip:xlconcat:2.1 status_bits]
set_property CONFIG.NUM_PORTS {4} $concat
set clkp [create_bd_port -dir I -type clk -freq_hz 50000000 clk_50]
connect_bd_net $clkp [get_bd_pins rgb_io/clk_50]
foreach {port width} {lcd_r 8 lcd_g 8 lcd_b 8 lcd_clk 1 lcd_de 1 lcd_hsync 1 lcd_vsync 1 lcd_bl 1 lcd_reset_n 1 tp_reset_n 1} {
    if {$width==1} {set p [create_bd_port -dir O $port]} else {set p [create_bd_port -dir O -from [expr {$width-1}] -to 0 $port]}
    connect_bd_net [get_bd_pins rgb_io/$port] $p
}
connect_bd_intf_net [get_bd_intf_pins ps7/M_AXI_GP0] [get_bd_intf_pins ctrl_ic/S00_AXI]
foreach {iface target} {M00_AXI vdma/S_AXI_LITE M01_AXI vtc/ctrl M02_AXI status_gpio/S_AXI} {
    connect_bd_intf_net [get_bd_intf_pins ctrl_ic/$iface] [get_bd_intf_pins $target]
}
connect_bd_intf_net [get_bd_intf_pins vdma/M_AXI_MM2S] [get_bd_intf_pins data_ic/S00_AXI]
connect_bd_intf_net [get_bd_intf_pins data_ic/M00_AXI] [get_bd_intf_pins ps7/S_AXI_HP0]
connect_bd_intf_net [get_bd_intf_pins vdma/M_AXIS_MM2S] [get_bd_intf_pins video_out/video_in]
connect_bd_intf_net [get_bd_intf_pins vtc/vtiming_out] [get_bd_intf_pins video_out/vtiming_in]
net ps7/FCLK_CLK0 ps7/M_AXI_GP0_ACLK ps7/S_AXI_HP0_ACLK ctrl_ic/ACLK ctrl_ic/S00_ACLK ctrl_ic/M00_ACLK ctrl_ic/M01_ACLK ctrl_ic/M02_ACLK \
    data_ic/ACLK data_ic/S00_ACLK data_ic/M00_ACLK vdma/s_axi_lite_aclk vdma/m_axi_mm2s_aclk \
    vtc/s_axi_aclk status_gpio/s_axi_aclk rst_axi/slowest_sync_clk status_cdc/clk
net rgb_io/pixel_clk rst_pixel/slowest_sync_clk vdma/m_axis_mm2s_aclk video_out/aclk vtc/clk
net ps7/FCLK_RESET0_N rst_axi/ext_reset_in rst_pixel/ext_reset_in
# Keep AXI-Lite diagnostics accessible even if the external pixel clock is lost.
# FCLK0 supplies the control domain; only the pixel reset depends on its MMCM.
one rst_axi/dcm_locked
net rgb_io/pixel_locked rst_pixel/dcm_locked status_bits/In3
foreach r {rst_axi rst_pixel} {
    # proc_sys_reset's auxiliary input is ACTIVE_LOW in this generated IP.
    # Deassert it at 1; tying it to 0 holds every peripheral in reset forever.
    one $r/aux_reset_in
    zero $r/mb_debug_sys_rst
}
net rst_axi/peripheral_aresetn vdma/axi_resetn vtc/s_axi_aresetn status_gpio/s_axi_aresetn status_cdc/resetn
net rst_axi/interconnect_aresetn ctrl_ic/ARESETN ctrl_ic/S00_ARESETN ctrl_ic/M00_ARESETN ctrl_ic/M01_ARESETN ctrl_ic/M02_ARESETN \
    data_ic/ARESETN data_ic/S00_ARESETN data_ic/M00_ARESETN
net rst_pixel/peripheral_aresetn video_out/aresetn vtc/resetn
one video_out/aclken;one video_out/vid_io_out_ce;zero video_out/fid
one vtc/clken;one vtc/s_axi_aclken;net video_out/vtg_ce vtc/gen_clken
optional_zero vtc/fsync_in;optional_zero vtc/det_clken
net video_out/vid_data rgb_io/video_data
net video_out/vid_active_video rgb_io/video_de
net video_out/vid_hsync rgb_io/video_hsync
net video_out/vid_vsync rgb_io/video_vsync
net video_out/locked rgb_io/video_locked status_bits/In0
net video_out/underflow status_bits/In1
net video_out/overflow status_bits/In2
net status_bits/dout status_cdc/async_status
net status_cdc/status status_gpio/gpio_io_i
assign_bd_address
set_property offset 0x43000000 [get_bd_addr_segs ps7/Data/SEG_vdma_Reg]
set_property offset 0x43C00000 [get_bd_addr_segs ps7/Data/SEG_vtc_Reg]
set_property offset 0x41200000 [get_bd_addr_segs ps7/Data/SEG_status_gpio_Reg]
validate_bd_design
if {[get_bd_nets -of_objects [get_bd_pins rst_axi/dcm_locked]] ne [get_bd_nets -of_objects [get_bd_pins const1/dout]]} {
    error "Control reset must be independent of pixel clock lock"
}
if {[get_property CONFIG.c_prmry_is_aclk_async $vdma] ne "1"} {error "VDMA async clock inference failed"}
foreach r {rst_axi rst_pixel} {
    if {[get_property CONFIG.C_EXT_RESET_HIGH [get_bd_cells $r]] ne "0"} {error "FCLK active-low reset polarity inference failed"}
    if {[get_property CONFIG.C_AUX_RESET_HIGH [get_bd_cells $r]] ne "0"} {error "Auxiliary reset polarity changed; recheck inactive tie-off"}
    if {[get_bd_nets -of_objects [get_bd_pins $r/aux_reset_in]] ne [get_bd_nets -of_objects [get_bd_pins const1/dout]]} {
        error "Auxiliary active-low reset must be deasserted"
    }
}
save_bd_design
set fd [open ps7_readback.txt w]
foreach p [lsort [list_property $ps]] {
 if {[string match CONFIG.PCW_* $p]} {puts $fd "$p=[get_property $p $ps]"}
}
close $fd
write_bd_tcl -force screen_system_recreate.tcl
set bd [get_files screen_system.bd]
generate_target all $bd
make_wrapper -files $bd -top
add_files [file join $project_dir sp2_ps_screen.gen sources_1 bd screen_system hdl screen_system_wrapper.v]
set_property top screen_system_wrapper [current_fileset]
update_compile_order -fileset sources_1
puts "BD_PASS: PS7 DDR16 UART52/53 VDMA24 VTC RGB"
