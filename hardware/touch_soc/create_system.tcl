set touch_here [file dirname [file normalize [info script]]]
source [file join $touch_here .. ps create_system.tcl]
# Keep the known-good PS/video sources intact; detach only the old reset tie-off.
delete_bd_objs [get_bd_ports tp_reset_n]
set tg [create_bd_cell -type ip -vlnv xilinx.com:ip:axi_gpio:2.0 touch_gpio]
set_property -dict [list CONFIG.C_GPIO_WIDTH {4} CONFIG.C_ALL_INPUTS {0} CONFIG.C_ALL_OUTPUTS {0} CONFIG.C_IS_DUAL {0} CONFIG.C_DOUT_DEFAULT {0x00000000} CONFIG.C_TRI_DEFAULT {0xFFFFFFFF}] $tg
set_property CONFIG.NUM_MI {4} [get_bd_cells ctrl_ic]
connect_bd_intf_net [get_bd_intf_pins ctrl_ic/M03_AXI] [get_bd_intf_pins touch_gpio/S_AXI]
net ps7/FCLK_CLK0 touch_gpio/s_axi_aclk ctrl_ic/M03_ACLK
net rst_axi/peripheral_aresetn touch_gpio/s_axi_aresetn
net rst_axi/interconnect_aresetn ctrl_ic/M03_ARESETN
set ts [create_bd_cell -type module -reference status_sync touch_cdc]
net ps7/FCLK_CLK0 touch_cdc/clk
net rst_axi/peripheral_aresetn touch_cdc/resetn
foreach {name dir pin} {touch_o O touch_gpio/gpio_io_o touch_t O touch_gpio/gpio_io_t touch_i I touch_cdc/async_status} {
 set p [create_bd_port -dir $dir -from 3 -to 0 $name]
 connect_bd_net $p [get_bd_pins $pin]
}
net touch_cdc/status touch_gpio/gpio_io_i
assign_bd_address
set seg [get_bd_addr_segs ps7/Data/SEG_touch_gpio_Reg]
set_property range 64K $seg
set_property offset 0x41210000 $seg
validate_bd_design
save_bd_design
write_bd_tcl -force touch_system_recreate.tcl
generate_target all [get_files screen_system.bd]
make_wrapper -files [get_files screen_system.bd] -top
set wrapper [file join $project_dir sp2_ps_screen.gen sources_1 bd screen_system hdl screen_system_wrapper.v]
set fd [open $wrapper r];set txt [read $fd];close $fd
# Add a top-level IOBUF layer without relying on inout module references in BD.
regsub {module screen_system_wrapper} $txt {module scope_touch_top} txt
set first [string first "(" $txt];set last [string first ");" $txt $first]
set names [split [string range $txt [expr {$first+1}] [expr {$last-1}]] ,]
set kept {}
foreach n $names {set n [string trim $n];if {$n ni {touch_i touch_o touch_t}} {lappend kept $n}}
lappend kept tp_sda tp_scl tp_int tp_reset_n
set txt [string replace $txt [expr {$first+1}] [expr {$last-1}] [join $kept ,\n]]
set txt [string map [list {input [3:0]touch_i;} {} {output [3:0]touch_o;} {} {output [3:0]touch_t;} {}] $txt]
set last [string first ");" $txt]
set txt "[string range $txt 0 [expr {$last+1}]]\n inout tp_sda,tp_scl,tp_int;\n output tp_reset_n;\n[string range $txt [expr {$last+2}] end]"
regsub {endmodule} $txt {touch_pads touch_io(.gpio_o(touch_o),.gpio_t(touch_t),.raw_i(touch_i),.tp_sda(tp_sda),.tp_scl(tp_scl),.tp_int(tp_int),.tp_reset_n(tp_reset_n));
endmodule} txt
set fd [open scope_touch_top.v w];puts $fd $txt;close $fd
add_files [list scope_touch_top.v [file join $touch_here touch_pads.v]]
add_files -fileset constrs_1 [file join $touch_here touch.xdc]
set_property top scope_touch_top [current_fileset]
update_compile_order -fileset sources_1
set_property STEPS.OPT_DESIGN.TCL.PRE [file join $touch_here timing_constraints.tcl] [get_runs impl_1]
puts "TOUCH_BD_PASS: GPIO41210000; PS7 and video retained; no ADC ports"
