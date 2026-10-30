`timescale 1ns/1ps
module tb_lcd_top;
  reg clk_50=0;
  wire [7:0] r,g,b;
  wire pclk,hs,vs,de,bl,rst_n,tp_rst_n;
  // This test isolates clock/IO integration. tb_powerup verifies DEFAULT long delays.
  lcd_top #(.PATTERN_MODE(2),.LCD_RESET_CYCLES(16),.BACKLIGHT_CYCLES(16)) dut(.clk_50(clk_50),.lcd_r(r),.lcd_g(g),.lcd_b(b),
    .lcd_clk(pclk),.lcd_hsync(hs),.lcd_vsync(vs),.lcd_de(de),
    .lcd_bl(bl),.lcd_reset_n(rst_n),.tp_reset_n(tp_rst_n));
  // UNISIM toggles even unconnected spare outputs. Slow only those in simulation;
  // CLKOUT0=51.2 MHz, feedback and scan dimensions stay exact.
  defparam dut.pixel_mmcm.CLKOUT1_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT2_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT3_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT4_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT5_DIVIDE=128;
  defparam dut.pixel_mmcm.CLKOUT6_DIVIDE=128;
  always #10 clk_50=~clk_50;
  realtime locked_at,reset_at,bl_at,last_pixel_edge,last_data_change,period;
  integer edges=0,active=0;
  reg monitor_clock=0;
  always @(r or g or b or de or hs or vs)last_data_change=$realtime;
  always @(posedge pclk)begin
    if(monitor_clock)begin
      if(edges>0)begin
        period=$realtime-last_pixel_edge;
        if(period<19.45 || period>19.62)$fatal(1,"Pixel period not 51.2 MHz: %f ns",period);
      end
      last_pixel_edge=$realtime;edges=edges+1;
      if(rst_n && $realtime-last_data_change<8.0)$fatal(1,"Insufficient data setup to forwarded rising DCLK");
      if(!rst_n && de!==0)$fatal(1,"DE while LCD reset asserted");
      if(de && ^{r,g,b}===1'bx)$fatal(1,"Undefined registered pixel");
    end
  end
  initial begin
    wait(dut.locked===1);locked_at=$realtime;monitor_clock=1;
    wait(rst_n===1);reset_at=$realtime;
    $display("INFO: LCD reset released at %0.3f ms",(reset_at-locked_at)/1e6);
    if(reset_at-locked_at<300 || reset_at-locked_at>450)$fatal(1,"Accelerated reset hold failed");
    if(bl!==0 || tp_rst_n!==0)$fatal(1,"Backlight/touch enable before intended");
    wait(bl===1);bl_at=$realtime;
    $display("INFO: backlight enabled at %0.3f ms",(bl_at-locked_at)/1e6);
    if(bl_at-locked_at>600)$fatal(1,"Accelerated backlight interval failed");
    if(de!==0)$fatal(1,"Backlight enabled during active line");
    // First active line through the actual forwarded clock and registered outputs.
    wait(de===1);
    repeat(1024)begin @(posedge pclk);if(de)active=active+1;end
    @(posedge pclk);if(de!==0)$fatal(1,"Top-level line not 1024 pixels");
    if(active!=1024)$fatal(1,"Top-level visible pixels in line: %0d",active);
    // Loss of lock must clear visible output and restart the full power-up sequence.
    monitor_clock=0;force dut.locked=0;
    #100;
    if(rst_n!==0 || bl!==0 || de!==0 || {r,g,b}!==0)$fatal(1,"Lost-lock blanking/reset failed");
    release dut.locked;
    wait(dut.reset===0);
    if(rst_n!==0 || bl!==0)$fatal(1,"Startup not restarted after lock recovery");
    $display("PASS: UNISIM MMCM/ODDR, 51.2 MHz, half-cycle setup, 1024-pixel registered line, lost-lock recovery");
    $finish;
  end
  initial begin #1500000;$fatal(1,"top-level watchdog");end
endmodule
