`timescale 1ns/1ps
module tb_adc_frontend;
 reg clk=0,resetn=0;always #10 clk=~clk;
 reg [9:0] d1=0,d2=0;reg o1=0,o2=0;
 wire c1,c2,oe1,oe2,sclk,ready,valid,locked;wire [9:0] q1,q2;wire t1,t2;
 adc_frontend dut(clk,resetn,d1,d2,o1,o2,c1,c2,oe1,oe2,sclk,ready,valid,locked,q1,q2,t1,t2);
 integer index=0,seen=0,edges=0,last_valid=0;
 integer hist1[0:23],hist2[0:23],hist_otr1[0:23],hist_otr2[0:23],h[0:23];
 integer i,j,k,phase=0,output_count=0,expect_valid=0,sum1,sum2,expect_otr1,expect_otr2;
 initial begin
  for(i=0;i<24;i=i+1)begin hist1[i]=0;hist2[i]=0;hist_otr1[i]=0;hist_otr2[i]=0;h[i]=0;end
  for(i=0;i<8;i=i+1)for(j=0;j<8;j=j+1)for(k=0;k<8;k=k+1)h[i+j+k]=h[i+j+k]+1;
 end
 realtime previous_rise=0;
 always @(posedge c1)begin
  if(previous_rise!=0&&($realtime-previous_rise<199.9||$realtime-previous_rise>200.1))$fatal(1,"conversion clock not5MHz");
  previous_rise=$realtime;
  if(!oe1)begin
   index=index+1;
   // Model 70ns worst budget. Output must be stable before opposite edge capture.
   #70;d1=index&1023;d2=(1023-index)&1023;o1=index&1;o2=(index>>1)&1;
  end
 end
 always @(posedge sclk)begin
  expect_valid=0;
  if(ready)begin
   for(i=23;i>0;i=i-1)begin hist1[i]=hist1[i-1];hist2[i]=hist2[i-1];hist_otr1[i]=hist_otr1[i-1];hist_otr2[i]=hist_otr2[i-1];end
   hist1[0]=dut.captured[9:0];hist2[0]=dut.captured[20:11];hist_otr1[0]=dut.captured[10];hist_otr2[0]=dut.captured[21];
   phase=phase+1;
   if(phase==8)begin phase=0;output_count=output_count+1;
    if(output_count>3)begin
     expect_valid=1;sum1=0;sum2=0;expect_otr1=0;expect_otr2=0;
     for(i=0;i<24;i=i+1)begin sum1=sum1+h[i]*hist1[i];sum2=sum2+h[i]*hist2[i];expect_otr1=expect_otr1|hist_otr1[i];expect_otr2=expect_otr2|hist_otr2[i];end
    end
   end
  end
  #1;
  if(resetn&&valid!==expect_valid)$fatal(1,"frontend filter cadence/warmup mismatch");
  if(resetn&&valid&&(q1!==(sum1+256)/512||q2!==(sum2+256)/512||t1!==expect_otr1||t2!==expect_otr2))$fatal(1,"frontend filtered data/OTR wiring mismatch");edges=edges+1;
  if(ready)begin
   if(oe1||oe2||!locked||c1!==0||c2!==0)$fatal(1,"ready/clock polarity/OE");
   if(dut.captured!=={o2,d2,o1,d1})$fatal(1,"data/OTR capture mismatch");
   if(valid)begin
    if(seen&&edges-last_valid!=8)$fatal(1,"record decimation not8");
    last_valid=edges;seen=seen+1;
   end
  end
 end
 initial begin
  #1;resetn=1;#1;resetn=0;#300;resetn=1;
  wait(ready);repeat(200)@(posedge sclk);
  if(seen<10)$fatal(1,"no valid samples");
  resetn=0;#1;if(ready||!oe1||!oe2)$fatal(1,"reset did not revoke ready/OE");
  $display("ADC_FRONTEND_PASS: 5MHz opposite-edge data/OTR, 70ns delay model, CIC3 8:1 filtered cadence, reset/OE");$finish;
 end
 initial begin #500000;$fatal(1,"frontend global timeout");end
endmodule
