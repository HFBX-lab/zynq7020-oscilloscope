`timescale 1ns/1ps
module cic_pipeline_check #(parameter R=10,PIPE=1,GAP=1)(output reg done=0);
 reg clk=0,rst=0,valid=0;always #5 clk=~clk;
 reg [9:0] a=0,b=0;reg oa=0,ob=0;
 wire v;wire [9:0] x,y;wire ox,oy;
 adc_cic_decimator #(.R(R),.PIPELINED_OUTPUT(PIPE)) dut(clk,rst,valid,a,b,oa,ob,v,x,y,ox,oy);
 integer ca[0:3*R-1],cb[0:3*R-1],co[0:3*R-1],cp[0:3*R-1],h[0:3*R-1];
 integer i,j,k,n,phase=0,outputs=0,ev=0,old_ev=0,sx,sy,ex,ey,eox,eoy,old_x,old_y,old_ox,old_oy;
 initial begin
  for(i=0;i<3*R;i=i+1)begin ca[i]=0;cb[i]=0;co[i]=0;cp[i]=0;h[i]=0;end
  for(i=0;i<R;i=i+1)for(j=0;j<R;j=j+1)for(k=0;k<R;k=k+1)h[i+j+k]=h[i+j+k]+1;
  #1;rst=1;#1;rst=0;#17;rst=1;
  for(n=0;n<12000;n=n+1)begin
   @(negedge clk);valid=(n%GAP==0)&&(n%7!=2);a=n<100?1023:(n<200?0:(n*37+17)%1024);
   b=n<100?0:(n<200?1023:(n*53+811)%1024);oa=(n%101==0);ob=(n%137==0);ev=0;
   if(valid)begin
    for(i=3*R-1;i>0;i=i-1)begin ca[i]=ca[i-1];cb[i]=cb[i-1];co[i]=co[i-1];cp[i]=cp[i-1];end
    ca[0]=a;cb[0]=b;co[0]=oa;cp[0]=ob;phase=phase+1;
    if(phase==R)begin
     phase=0;outputs=outputs+1;
     if(outputs>3)begin
      ev=1;sx=0;sy=0;eox=0;eoy=0;
      for(i=0;i<3*R;i=i+1)begin sx=sx+h[i]*ca[i];sy=sy+h[i]*cb[i];eox=eox|co[i];eoy=eoy|cp[i];end
      ex=(sx+R*R*R/2)/(R*R*R);ey=(sy+R*R*R/2)/(R*R*R);
     end
    end
   end
   @(posedge clk);#1;
   if(v!==(PIPE?old_ev:ev))$fatal(1,"CIC pipeline valid/cadence R%0d PIPE%0d GAP%0d n%0d",R,PIPE,GAP,n);
   if(v&&(x!==(PIPE?old_x:ex)||y!==(PIPE?old_y:ey)||ox!==(PIPE?old_ox:eox)||oy!==(PIPE?old_oy:eoy)))$fatal(1,"CIC pipeline convolution/data/OTR R%0d PIPE%0d GAP%0d n%0d",R,PIPE,GAP,n);
   old_ev=ev;old_x=ex;old_y=ey;old_ox=eox;old_oy=eoy;
  end
  // Force more input even for GAP8, then interrupt a genuinely queued word.
  if(PIPE)begin
   @(negedge clk);valid=1;
   wait(dut.pending_valid===1'b1);
  end
  @(negedge clk);
  if(PIPE&&dut.pending_valid!==1'b1)$fatal(1,"Queued-reset test missed pending boundary");
  rst=0;#1;if(v)$fatal(1,"CIC async reset leaked valid");
  // Release before the following clock, proving asynchronous pending flush.
  rst=1;
  @(posedge clk);#1;if(v)$fatal(1,"CIC pipeline reset failed to flush");done=1;
 end
endmodule
module tb_adc_cic_pipeline;
 wire a,b,c,d,e;
 cic_pipeline_check #(.R(10),.PIPE(1),.GAP(1)) p10(a);
 cic_pipeline_check #(.R(10),.PIPE(1),.GAP(8)) p10gap(b);
 cic_pipeline_check #(.R(8),.PIPE(1),.GAP(1)) p8(c);
 cic_pipeline_check #(.R(10),.PIPE(0),.GAP(1)) legacy10(d);
 cic_pipeline_check #(.R(8),.PIPE(0),.GAP(1)) legacy8(e);
 initial begin wait(a&&b&&c&&d&&e);$display("CIC_PIPELINE_PASS: independent convolution, PIPE0/1 R8/10, gaps including8clock input spacing, exact1clock latency, OTR/reset");$finish;end
 initial begin #200000;$fatal(1,"CIC pipeline timeout");end
endmodule
