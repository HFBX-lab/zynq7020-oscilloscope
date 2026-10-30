`timescale 1ns/1ps
module tb_rgb_io;
  reg clk=0,valid_video=0,de=0,hs=0,vs=0;
  reg [23:0] data=24'h123456;
  wire pixel,locked,lde,lhs,lvs,bl,rst,tp;
  wire [7:0] r,g,b;wire dclk;
  screen_rgb_io dut(.clk_50(clk),.pixel_clk(pixel),.pixel_locked(locked),
    .video_data(data),.video_de(de),.video_hsync(hs),.video_vsync(vs),.video_locked(valid_video),
    .lcd_r(r),.lcd_g(g),.lcd_b(b),.lcd_clk(dclk),.lcd_de(lde),.lcd_hsync(lhs),.lcd_vsync(lvs),
    .lcd_bl(bl),.lcd_reset_n(rst),.tp_reset_n(tp));
  defparam dut.powerup.LCD_RESET_CYCLES=4;
  defparam dut.powerup.BACKLIGHT_CYCLES=8;
  defparam dut.pixel_mmcm.CLKOUT1_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT2_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT3_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT4_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT5_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT6_DIVIDE=128;
  always #10 clk=~clk;
  initial begin
    wait(locked===1);repeat(10)@(negedge pixel);
    if(rst!==0 || bl!==0 || lde!==0)$fatal(1,"Video unlocked must keep panel reset/blank");
    valid_video=1;repeat(14)@(negedge pixel);
    if(rst!==1 || bl!==0)$fatal(1,"Panel reset release / wait for frame");
    if(lhs!==1 || lvs!==1)$fatal(1,"Positive internal sync must invert at panel pins");
    vs=1;repeat(3)@(negedge pixel);
    if(bl!==1 || lvs!==0)$fatal(1,"Backlight frame boundary / panel negative VSYNC");
    vs=0;hs=1;de=1;data=24'hff0000;repeat(2)@(negedge pixel);
    if(lhs!==0 || lde!==1 || {r,g,b}!==24'hff0000)$fatal(1,"Registered RGB/DE/HSYNC mapping");
    de=0;repeat(2)@(negedge pixel);
    if({r,g,b}!==0 || lde!==0)$fatal(1,"Blanking RGB");
    valid_video=0;#1;
    if(rst!==0 || bl!==0 || lde!==0 || {r,g,b}!==0)$fatal(1,"Loss of video lock must blank/reset immediately");
    if(tp!==0)$fatal(1,"Touch must remain reset");
    $display("PASS: PS RGB layer internal-positive/panel-negative sync, RGB packing, frame backlight, blanking and lost-video reset");
    $finish;
  end
  initial begin #100000;$fatal(1,"RGB layer watchdog");end
endmodule
