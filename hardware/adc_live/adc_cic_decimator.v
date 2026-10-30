`timescale 1ns/1ps
// Three cascaded integrators/combs, unsigned dual-channel ADC codes.
// Arithmetic is modulo 2^W; the final nonnegative FIR result fits W bits.
// Valid inputs need not arrive on consecutive clocks. No downsampling aliases
// can be repaired if they were already introduced before this filter's input.
module adc_cic_decimator #(parameter integer R=8,PIPELINED_OUTPUT=0)(
 input wire clk,input wire reset_n,input wire in_valid,
 input wire [9:0] in1,in2,input wire otr1,otr2,
 output reg out_valid,output reg [9:0] out1,out2,
 output reg out_otr1,out_otr2
);
 localparam integer W=11+3*$clog2(R),GAIN=R*R*R;
 reg [W-1:0] a1,a2,a3,b1,b2,b3,az1,az2,az3,bz1,bz2,bz3;
 reg [$clog2(R)-1:0] phase;
 reg [1:0] warm;
 reg block1,block2;reg [1:0] prior1,prior2;
 reg pending_valid,pending_otr1,pending_otr2;
 reg [W-1:0] pending1,pending2;
 wire [W-1:0] an1=a1+in1,an2=a2+an1,an3=a3+an2;
 wire [W-1:0] bn1=b1+in2,bn2=b2+bn1,bn3=b3+bn2;
 wire [W-1:0] ac1=an3-az1,ac2=ac1-az2,ac3=ac2-az3;
 wire [W-1:0] bc1=bn3-bz1,bc2=bc1-bz2,bc3=bc2-bz3;
 wire [W-1:0] normalized1=((PIPELINED_OUTPUT?pending1:ac3)+GAIN/2)/GAIN;
 wire [W-1:0] normalized2=((PIPELINED_OUTPUT?pending2:bc3)+GAIN/2)/GAIN;
 always @(posedge clk or negedge reset_n)begin
  if(!reset_n)begin
   a1<=0;a2<=0;a3<=0;b1<=0;b2<=0;b3<=0;
   az1<=0;az2<=0;az3<=0;bz1<=0;bz2<=0;bz3<=0;
   phase<=0;warm<=0;block1<=0;block2<=0;prior1<=0;prior2<=0;
   out_valid<=0;out1<=0;out2<=0;out_otr1<=0;out_otr2<=0;
   pending_valid<=0;pending1<=0;pending2<=0;pending_otr1<=0;pending_otr2<=0;
  end else begin
   out_valid<=0;
   pending_valid<=0;
   // Normalize one clock after the comb result, independently of in_valid.
   // Low-speed profiles retain their original zero-extra-latency behavior.
   if(PIPELINED_OUTPUT&&pending_valid)begin
    out_valid<=1;out1<=normalized1[9:0];out2<=normalized2[9:0];
    out_otr1<=pending_otr1;out_otr2<=pending_otr2;
   end
   if(in_valid)begin
    a1<=an1;a2<=an2;a3<=an3;b1<=bn1;b2<=bn2;b3<=bn3;
    if(phase==R-1)begin
     phase<=0;az1<=an3;az2<=ac1;az3<=ac2;bz1<=bn3;bz2<=bc1;bz3<=bc2;
     prior1<={prior1[0],block1|otr1};prior2<={prior2[0],block2|otr2};block1<=0;block2<=0;
     if(warm!=3)warm<=warm+1'b1;
     else if(PIPELINED_OUTPUT)begin
      pending_valid<=1;pending1<=ac3;pending2<=bc3;
      pending_otr1<=block1|otr1|(|prior1);pending_otr2<=block2|otr2|(|prior2);
     end
     else begin
      out_valid<=1;out1<=normalized1[9:0];out2<=normalized2[9:0];
      // Conservative OR over three complete blocks (FIR support is 3R-2).
      out_otr1<=block1|otr1|(|prior1);out_otr2<=block2|otr2|(|prior2);
     end
    end else begin phase<=phase+1'b1;block1<=block1|otr1;block2<=block2|otr2;end
   end
  end
 end
endmodule
