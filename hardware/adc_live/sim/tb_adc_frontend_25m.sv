`timescale 1ns/1ps
module frontend_25m_check #(parameter real RETURN_NS=25.0,DRIVE_PHASE=315.0,parameter integer PIPELINE=4,MODE=0)(output reg finished=0);
 reg clk=0,rst=0;always #10 clk=~clk;
 reg [9:0] d1=0,d2=0;reg o1=0,o2=0;
 wire c1,c2,oe1,oe2,sc,ready,valid,locked,rv;wire [9:0] q1,q2,r1,r2;wire t1,t2,ro1,ro2;
 adc_frontend #(.CONVERSION_HZ(25000000),.DRIVE_PHASE_DEG(DRIVE_PHASE),.BASE_DECIMATION(4)) dut(.clk_50(clk),.resetn(rst),.ad_data_1(d1),.ad_data_2(d2),.ad_otr_1(o1),.ad_otr_2(o2),
 .ad_clk_1(c1),.ad_clk_2(c2),.ad_oe_1(oe1),.ad_oe_2(oe2),.sample_clk(sc),.sample_ready(ready),.sample_valid(valid),.clock_locked(locked),
 .data1(q1),.data2(q2),.otr1(t1),.otr2(t2),.raw_valid(rv),.raw_data1(r1),.raw_data2(r2),.raw_otr1(ro1),.raw_otr2(ro2));
 integer launch=0,captured=0,tag,i,j,k,phase=0,outputs=0,ev=0,s1,s2,x1,x2;
 integer h[0:11],a[0:11],b[0:11],oa[0:11],ob[0:11];
 realtime last_rise=0,last_fall=0,last_capture=0;real expected_time;
 function integer code1(input integer n,input real ns);
  if(MODE==0)code1=n&1023;else code1=$rtoi(512.0+100.0*$sin(6.28318530718*100000.0*ns/1.0e9)+0.5);
 endfunction
 function integer code2(input integer n,input real ns);
  if(MODE==0)code2=((1023-(n&1023))^((n>>10)&1023))&1023;
  else code2=$rtoi(520.0+77.0*$cos(6.28318530718*170000.0*ns/1.0e9)+0.5);
 endfunction
 initial begin
  for(i=0;i<12;i=i+1)begin h[i]=0;a[i]=0;b[i]=0;oa[i]=0;ob[i]=0;end
  for(i=0;i<4;i=i+1)for(j=0;j<4;j=j+1)for(k=0;k<4;k=k+1)h[i+j+k]=h[i+j+k]+1;
  #1;rst=1;#1;rst=0;#300;rst=1;
  wait(ready);repeat(5000)@(negedge sc);
  if(captured<4900||outputs<1200)$fatal(1,"missing RAW25/CIC4 outputs");
  rst=0;#1;if(ready||rv||!oe1||!oe2)$fatal(1,"reset leaked ready/OE");finished=1;
 end
 always @(posedge c1)begin
  if(last_rise!=0&&($realtime-last_rise<39.9||$realtime-last_rise>40.1))$fatal(1,"ADC conversion clock not25MHz");
  last_rise=$realtime;
  if(rst&&last_fall!=0&&$realtime-last_fall<19.9)$fatal(1,"ADC low pulse too short");
  if(!oe1)begin
   launch=launch+1;
   // Delayed nonblocking assignments permit multiple ADC outputs in flight.
   // Samples derive from launch time, not from the FPGA's capture clock.
   d1<=#(RETURN_NS) code1(launch-PIPELINE,$realtime-PIPELINE*40.0);
   d2<=#(RETURN_NS) code2(launch-PIPELINE,$realtime-PIPELINE*40.0);
   o1<=#(RETURN_NS) (MODE==0?((launch-PIPELINE)&1):0);
   o2<=#(RETURN_NS) (MODE==0?(((launch-PIPELINE)>>1)&1):0);
  end
 end
 always @(negedge c1)begin if(rst&&last_rise!=0&&$realtime-last_rise<19.9)$fatal(1,"ADC high pulse too short");last_fall=$realtime;end
 always @(posedge sc)begin
  ev=0;
  if(ready)begin
   if(last_capture!=0&&($realtime-last_capture<39.9||$realtime-last_capture>40.1))$fatal(1,"RAW cadence not25MSa/s");
   // UNISIM ODDR Q has a100ps model delay; retain200ps phase tolerance.
   if($realtime-last_rise<40.0-DRIVE_PHASE*40.0/360.0-0.2||$realtime-last_rise>40.0-DRIVE_PHASE*40.0/360.0+0.2)$fatal(1,"drive/capture phase mismatch delta=%0.3f phase=%0.3f",$realtime-last_rise,DRIVE_PHASE);
   last_capture=$realtime;tag=launch-PIPELINE-1;expected_time=last_rise-(PIPELINE+1)*40.0;captured=captured+1;
   for(i=11;i>0;i=i-1)begin a[i]=a[i-1];b[i]=b[i-1];oa[i]=oa[i-1];ob[i]=ob[i-1];end
   a[0]=dut.captured[9:0];b[0]=dut.captured[20:11];oa[0]=dut.captured[10];ob[0]=dut.captured[21];phase=phase+1;
   if(phase==4)begin
    phase=0;outputs=outputs+1;
    if(outputs>3)begin ev=1;s1=0;s2=0;x1=0;x2=0;
     for(i=0;i<12;i=i+1)begin s1=s1+h[i]*a[i];s2=s2+h[i]*b[i];x1=x1|oa[i];x2=x2|ob[i];end
    end
   end
   #1;
   if(!rv||oe1||oe2||!locked||c1!==c2)$fatal(1,"RAW ready/OE/dualclock mismatch");
   if(r1!==code1(tag,expected_time)||r2!==code2(tag,expected_time)||ro1!==(MODE==0?(tag&1):0)||ro2!==(MODE==0?((tag>>1)&1):0))
    $fatal(1,"ADC25_RETURN_APERTURE_ERROR delay=%0.1f got=%0d expected=%0d",RETURN_NS,r1,code1(tag,expected_time));
   if(valid!==ev)$fatal(1,"CIC4 output cadence/startup");
   if(valid&&(q1!==(s1+32)/64||q2!==(s2+32)/64||t1!==x1||t2!==x2))$fatal(1,"CIC4 independent convolution/OTR mismatch");
  end
 end
endmodule
module tb_adc_frontend_25m;
 wire a,b,c,d,e,f,g,h;frontend_25m_check #(.RETURN_NS(15.0),.PIPELINE(3)) x(a);
 frontend_25m_check #(.RETURN_NS(25.0),.PIPELINE(4)) y(b);
 frontend_25m_check #(.RETURN_NS(35.0),.PIPELINE(4)) z(c);
 frontend_25m_check #(.RETURN_NS(25.0),.PIPELINE(4),.MODE(1)) analog_time(d);
 frontend_25m_check #(.RETURN_NS(15.0),.DRIVE_PHASE(300.0)) early300(e);
 frontend_25m_check #(.RETURN_NS(35.0),.DRIVE_PHASE(300.0)) late300(f);
 frontend_25m_check #(.RETURN_NS(15.0),.DRIVE_PHASE(330.0)) early330(g);
 frontend_25m_check #(.RETURN_NS(35.0),.DRIVE_PHASE(330.0)) late330(h);
 initial begin wait(a&&b&&c&&d&&e&&f&&g&&h);$display("FRONTEND_25M_PASS: 40ns RAW/tagged/time-function,6.25M CIC4 convolution,15/25/35ns candidate,3/4pipeline,300/315/330phase,duty/reset");$finish;end
 initial begin #500000;$fatal(1,"frontend25M timeout");end
endmodule
module tb_adc_frontend_25m_late;
 wire done;frontend_25m_check #(.RETURN_NS(46.0)) x(done);
 initial begin wait(done);$fatal(1,"Late ADC output unexpectedly accepted");end
 initial begin #500000;$fatal(1,"late frontend timeout");end
endmodule
module tb_adc_frontend_25m_early;
 wire done;frontend_25m_check #(.RETURN_NS(4.0)) x(done);
 initial begin wait(done);$fatal(1,"Early ADC output unexpectedly accepted");end
 initial begin #500000;$fatal(1,"early frontend timeout");end
endmodule
