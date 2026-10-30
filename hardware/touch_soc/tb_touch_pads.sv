`timescale 1ns/1ps
module tb_touch_pads;
 reg [3:0] o=0,t=15;wire [3:0] i;
 tri1 sda,scl,intr;wire rst;
 reg slave_low=0;
 assign sda=slave_low?1'b0:1'bz;
 touch_pads dut(o,t,i,sda,scl,intr,rst);
 initial begin
  #10;if(!sda||!scl||!intr||rst)$fatal(1,"startup must release bus and hold reset");
  t=8;#10;if(sda||scl||intr||rst)$fatal(1,"pull low");
  o=15;t=0;#10;if(!sda||!scl||!intr||!rst)$fatal(1,"ones must release, never drive high");
  slave_low=1;#10;if(sda!==0||i[0]!==0)$fatal(1,"slave ACK must not contend with output high");
  slave_low=0;t=15;#10;if(rst)$fatal(1,"tristated reset must assert");
  $display("TOUCH_PADS_TEST_PASS: open drain, slave ACK, safe reset");$finish;
 end
endmodule
