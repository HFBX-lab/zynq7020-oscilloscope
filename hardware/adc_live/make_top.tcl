set wrapper [file join $project_dir sp2_ps_screen.gen sources_1 bd screen_system hdl screen_system_wrapper.v]
set fd [open $wrapper r];set txt [read $fd];close $fd
# Add a top-level IOBUF layer without relying on inout module references in BD.
regsub {module screen_system_wrapper} $txt {module scope_live_top} txt
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
set fd [open scope_live_top.v w];puts $fd $txt;close $fd
add_files [list scope_live_top.v [file join $live_here .. touch_soc touch_pads.v]]
set_property top scope_live_top [current_fileset]
update_compile_order -fileset sources_1
