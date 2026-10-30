`timescale 1ns/1ps
module tb_powerup;
  reg clk=0,reset=1;
  wire rst_n,bl,fs,de;
  wire hs,vs;wire [11:0] x,y;
  lcd_powerup dut(.pixel_clk(clk),.reset(reset),.frame_start(fs),.lcd_reset_n(rst_n),.lcd_bl(bl));
  lcd_timing scan(.pixel_clk(clk),.reset(reset || !rst_n),.frame_start(fs),
    .hsync(hs),.vsync(vs),.de(de),.pixel_x(x),.pixel_y(y));
  always #9.765625 clk=~clk;
  realtime began,reset_at,bl_at;
  integer active=0;
  reg last_bl=0,frame_at_edge;
  always @(posedge clk)begin
    frame_at_edge=fs;
    #1;
    if(bl && !last_bl && !frame_at_edge)$fatal(1,"Backlight did not rise at frame start");
    if(bl && !rst_n)$fatal(1,"Backlight on while panel is reset");
    last_bl=bl;
  end
  initial begin
    repeat(3)@(posedge clk);@(negedge clk);reset=0;began=$realtime;
    wait(rst_n===1);reset_at=$realtime;
    if(reset_at-began<9999000 || reset_at-began>10001000)$fatal(1,"Production 10ms reset count");
    if(bl!==0)$fatal(1,"Early backlight");
    wait(bl===1);bl_at=$realtime;
    if(bl_at-began<163840000 || bl_at-began>181000000)$fatal(1,"Production backlight count");
    repeat(1344*635)begin @(negedge clk);if(de)active=active+1;end
    if(active!=614400)$fatal(1,"Incomplete first visible frame after backlight: %0d",active);
    #2;reset=1;#1;
    if(rst_n!==0 || bl!==0 || de!==0)$fatal(1,"Asynchronous blanking/reset");
    @(negedge clk);reset=0;
    repeat(1000)@(posedge clk);
    if(rst_n!==0 || bl!==0)$fatal(1,"Power-up interval not restarted");
    $display("PASS: production 512000/8388608 startup counts, frame-aligned backlight, 614400 visible pixels, async restart");
    $display("INFO: production LCD reset=%0.3f ms, backlight=%0.3f ms",(reset_at-began)/1e6,(bl_at-began)/1e6);
    $finish;
  end
  initial begin #230000000;$fatal(1,"power-up watchdog");end
endmodule
