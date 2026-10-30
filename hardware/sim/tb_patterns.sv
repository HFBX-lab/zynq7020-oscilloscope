`timescale 1ns/1ps
module tb_patterns;
  reg clk=0, reset=1;
  reg [1:0] mode=0;
  wire hs,vs,de,frame_start;
  wire [11:0] x,y;
  wire [23:0] rgb;
  lcd_timing scan(.pixel_clk(clk),.reset(reset),.hsync(hs),.vsync(vs),.de(de),
    .pixel_x(x),.pixel_y(y),.frame_start(frame_start));
  lcd_test_pattern image(.pixel_clk(clk),.reset(reset),.mode(mode),.de(de),
    .pixel_x(x),.pixel_y(y),.frame_start(frame_start),.rgb(rgb));
  always #9.765625 clk=~clk;
  integer fd,count,n,m;
  integer starts;
  initial begin
    for(m=0;m<3;m=m+1)begin
      @(negedge clk);reset=1;mode=m;
      repeat(3)@(posedge clk);
      @(negedge clk);reset=0;
      case(m)
        0:fd=$fopen("../../../artifacts/rtl/colorbars.ppm","wb");
        1:fd=$fopen("../../../artifacts/rtl/grid.ppm","wb");
        2:fd=$fopen("../../../artifacts/rtl/waveform.ppm","wb");
      endcase
      if(fd==0)$fatal(1,"Cannot open frame output");
      $fwrite(fd,"P6\n1024 600\n255\n");count=0;starts=0;
      for(n=0;n<1344*635;n=n+1)begin
        #1;
        if(frame_start)starts=starts+1;
        if(de)begin
          if(x!==count%1024 || y!==count/1024)$fatal(1,"Pixel coordinates are not raster ordered");
          if(^rgb===1'bx)$fatal(1,"Undefined visible pixel");
          $fwrite(fd,"%c%c%c",rgb[23:16],rgb[15:8],rgb[7:0]);
          count=count+1;
        end else begin
          if(rgb!==0 || x!==0 || y!==0)$fatal(1,"Blanking must zero RGB/coordinates");
        end
        @(negedge clk);
      end
      if(count!=1024*600 || starts!=1)$fatal(1,"Frame dimensions or frame-start count");
      $fclose(fd);
      $display("PASS: pattern %0d, 614400 raster pixels, deterministic blanking",m);
    end
    $display("PASS: all three PL patterns and frame captures");$finish;
  end
  initial begin #60000000;$fatal(1,"pattern watchdog");end
endmodule
