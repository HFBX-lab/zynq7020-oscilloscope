`timescale 1ns/1ps
module frontend_10m_check #(parameter real RETURN_NS=70.0,parameter integer PIPELINE=4)(output reg finished=0);
 reg clk=0,rst=0;always #10 clk=~clk;
 reg [9:0] d1=0,d2=0;reg o1=0,o2=0;
 wire c1,c2,oe1,oe2,sc,ready,valid,locked,raw_valid;wire [9:0] q1,q2,raw1,raw2;wire t1,t2,ro1,ro2;
 adc_frontend #(.CONVERSION_HZ(10000000),.DRIVE_PHASE_DEG(45.0),.BASE_DECIMATION(16)) dut(.clk_50(clk),.resetn(rst),.ad_data_1(d1),.ad_data_2(d2),.ad_otr_1(o1),.ad_otr_2(o2),
 .ad_clk_1(c1),.ad_clk_2(c2),.ad_oe_1(oe1),.ad_oe_2(oe2),.sample_clk(sc),.sample_ready(ready),.sample_valid(valid),.clock_locked(locked),
 .data1(q1),.data2(q2),.otr1(t1),.otr2(t2),.raw_valid(raw_valid),.raw_data1(raw1),.raw_data2(raw2),.raw_otr1(ro1),.raw_otr2(ro2));
 integer launch=0,captured=0,tag,i,j,k,phase=0,outputs=0,expected_valid=0,s1,s2,x1,x2;
 integer h[0:47],a[0:47],b[0:47],oa[0:47],ob[0:47];realtime last_rise=0,last_fall=0,last_capture=0;
 initial begin
  for(i=0;i<48;i=i+1)begin h[i]=0;a[i]=0;b[i]=0;oa[i]=0;ob[i]=0;end
  for(i=0;i<16;i=i+1)for(j=0;j<16;j=j+1)for(k=0;k<16;k=k+1)h[i+j+k]=h[i+j+k]+1;
  #1;rst=1;#1;rst=0;#300;rst=1;
  wait(ready);repeat(1600)@(negedge sc);
  if(captured<1500||outputs<90)$fatal(1,"missing RAW/CIC outputs");
  rst=0;#1;if(ready||raw_valid||!oe1||!oe2)$fatal(1,"reset leaked ready/OE");finished=1;
 end
 always @(posedge c1)begin
  if(last_rise!=0&&($realtime-last_rise<99.9||$realtime-last_rise>100.1))$fatal(1,"ADC conversion clock not10MHz");
  last_rise=$realtime;
  if(rst&&last_fall!=0&&$realtime-last_fall<49.9)$fatal(1,"ADC low pulse too short");
  if(!oe1)begin launch=launch+1;#(RETURN_NS);d1=(launch-PIPELINE)&1023;d2=((launch-PIPELINE)*19+37)&1023;o1=(launch-PIPELINE)&1;o2=((launch-PIPELINE)>>1)&1;end
 end
 always @(negedge c1)begin if(rst&&last_rise!=0&&$realtime-last_rise<49.9)$fatal(1,"ADC high pulse too short");last_fall=$realtime;end
 always @(posedge sc)begin
  expected_valid=0;
  if(ready)begin
   if(last_capture!=0&&($realtime-last_capture<99.9||$realtime-last_capture>100.1))$fatal(1,"RAW cadence not10MSa/s");
   last_capture=$realtime;tag=(launch-PIPELINE)&1023;captured=captured+1;
   // CIC consumes the previous IOB-register value on this clock edge.
   for(i=47;i>0;i=i-1)begin a[i]=a[i-1];b[i]=b[i-1];oa[i]=oa[i-1];ob[i]=ob[i-1];end
   a[0]=dut.captured[9:0];b[0]=dut.captured[20:11];oa[0]=dut.captured[10];ob[0]=dut.captured[21];phase=phase+1;
   if(phase==16)begin
    phase=0;outputs=outputs+1;
    if(outputs>3)begin expected_valid=1;s1=0;s2=0;x1=0;x2=0;
     for(i=0;i<48;i=i+1)begin s1=s1+h[i]*a[i];s2=s2+h[i]*b[i];x1=x1|oa[i];x2=x2|ob[i];end
    end
   end
   #1;
   if(!raw_valid||oe1||oe2||!locked)$fatal(1,"RAW ready/OE mismatch");
   if(raw1!==tag||raw2!==((tag*19+37)&1023)||ro1!==(tag&1)||ro2!==((tag>>1)&1))$fatal(1,"ADC_RETURN_APERTURE_ERROR delay=%0.1f got=%0d expected=%0d",RETURN_NS,raw1,tag);
   if(valid!==expected_valid)$fatal(1,"CIC16 output cadence/startup");
   if(valid&&(q1!==(s1+2048)/4096||q2!==(s2+2048)/4096||t1!==x1||t2!==x2))$fatal(1,"CIC16 data/OTR mismatch");
  end
 end
endmodule
module tb_adc_frontend_10m;
 wire a,b,c;frontend_10m_check #(.RETURN_NS(0.0),.PIPELINE(3)) x(a);
 frontend_10m_check #(.RETURN_NS(25.0),.PIPELINE(4)) y(b);
 frontend_10m_check #(.RETURN_NS(70.0),.PIPELINE(4)) z(c);
 initial begin wait(a&&b&&c);$display("FRONTEND_10M_PASS: taggedRAW10MSa/s, CIC16 independentconvolution,0/25/70nsreturn,3/4cyclepipeline,clock/duty/reset");$finish;end
 initial begin #500000;$fatal(1,"frontend10M timeout");end
endmodule
module tb_adc_frontend_10m_bad;
 wire done;frontend_10m_check #(.RETURN_NS(88.0)) x(done);
 initial begin wait(done);$fatal(1,"Late ADC output unexpectedly accepted");end
 initial begin #500000;$fatal(1,"bad frontend timeout");end
endmodule
