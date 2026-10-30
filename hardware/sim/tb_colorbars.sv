`timescale 1ns/1ps
module tb_colorbars;
  reg clk=0, rst=1;
  wire hs,vs,de;
  wire [23:0] rgb;
  lcd_colorbars dut(.pixel_clk(clk),.reset(rst),.hsync(hs),.vsync(vs),.de(de),.rgb(rgb));
  always #9.765625 clk=~clk;
  integer n,h,v,active,hs_low,vs_low,bar;
  reg expected_de;
  reg [23:0] expected_rgb;
  reg [23:0] colors[0:7];
  initial begin
    colors[0]=24'hffffff;colors[1]=24'hffff00;colors[2]=24'h00ffff;colors[3]=24'h00ff00;
    colors[4]=24'hff00ff;colors[5]=24'hff0000;colors[6]=24'h0000ff;colors[7]=24'h000000;
    repeat(3) @(posedge clk);
    #1;
    if(de!==0 || rgb!==0) $fatal(1,"reset must blank output");
    @(negedge clk); rst=0;
    active=0;hs_low=0;vs_low=0;
    // Begin with h=v=0 immediately after reset; sample before each next rising edge.
    for(n=0;n<1344*635*2;n=n+1) begin
      #1;
      h=n%1344;v=(n/1344)%635;
      expected_de=(h>=160 && h<1184 && v>=23 && v<623);
      if(de!==expected_de) $fatal(1,"DE mismatch h=%0d v=%0d",h,v);
      if(hs!==(h>=20)) $fatal(1,"HSYNC mismatch");
      if(vs!==(v>=3)) $fatal(1,"VSYNC mismatch");
      expected_rgb=0;
      if(expected_de)begin bar=(h-160)/128;expected_rgb=colors[bar];active=active+1;end
      if(rgb!==expected_rgb) $fatal(1,"RGB mismatch h=%0d v=%0d",h,v);
      if(!hs)hs_low=hs_low+1;
      if(!vs)vs_low=vs_low+1;
      @(negedge clk);
    end
    if(active!=2*1024*600) $fatal(1,"active pixel total");
    if(hs_low!=2*20*635 || vs_low!=2*3*1344) $fatal(1,"sync width total");
    rst=1;@(posedge clk);#1;
    if(de!==0 || rgb!==0) $fatal(1,"mid-frame reset blanking");
    $display("PASS: two 1344x635 frames, 1228800 active pixels, 8 colors, sync widths, blanking, reset");
    $finish;
  end
  initial begin #40000000; $fatal(1,"simulation watchdog"); end
endmodule
