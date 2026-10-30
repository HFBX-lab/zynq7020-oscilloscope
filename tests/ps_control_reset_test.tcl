# Offline Vivado BD regression. No hardware-server operations.
open_project [lindex $argv 0]
open_bd_design [get_files screen_system.bd]
set ctrl [get_bd_nets -of_objects [get_bd_pins rst_axi/dcm_locked]]
set independent [get_bd_nets -of_objects [get_bd_pins const1/dout]]
if {$ctrl ne $independent} {error "Control AXI reset depends on pixel lock; missing pixel clock can hang MMIO"}
set pixel [get_bd_nets -of_objects [get_bd_pins rst_pixel/dcm_locked]]
set lock [get_bd_nets -of_objects [get_bd_pins rgb_io/pixel_locked]]
if {$pixel ne $lock} {error "Pixel reset must still wait for pixel clock lock"}
foreach block {rst_axi rst_pixel} {
    set active [get_property CONFIG.C_AUX_RESET_HIGH [get_bd_cells $block]]
    set inactive [expr {!$active}]
    set actual [get_bd_nets -of_objects [get_bd_pins $block/aux_reset_in]]
    set expected [get_bd_nets -of_objects [get_bd_pins const$inactive/dout]]
    if {$actual ne $expected} {error "$block auxiliary reset permanently asserted: active=$active"}
}
puts "PASS: control reset independent of pixel lock; pixel reset qualified; auxiliary resets deasserted"
close_project
