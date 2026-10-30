`timescale 1ns/1ps
module cic_check #(parameter R=8)(output reg done=0);
 reg clk=0,rst=0,valid=0;always #5 clk=~clk;
 reg [9:0] a=0,b=0;reg oa=0,ob=0;
 wire v;wire [9:0] x,y;wire ox,oy;
 adc_cic_decimator #(.R(R)) dut(clk,rst,valid,a,b,oa,ob,v,x,y,ox,oy);
 integer ca[0:3*R-1],cb[0:3*R-1],co[0:3*R-1],cp[0:3*R-1];
 integer h[0:3*R-1];integer i,j,k,n,ticks=0,phase=0,outputs=0;
 integer sx,sy,ex,ey,eox,eoy;
 initial begin
  for(i=0;i<3*R;i=i+1)begin ca[i]=0;cb[i]=0;co[i]=0;cp[i]=0;h[i]=0;end
  for(i=0;i<R;i=i+1)for(j=0;j<R;j=j+1)for(k=0;k<R;k=k+1)h[i+j+k]=h[i+j+k]+1;
  #1;rst=1;#1;rst=0;#17;rst=1;
  for(n=0;n<2400;n=n+1)begin
   @(negedge clk);valid=(n%7!=2);a=n<100?1023:(n<200?0:(n*37+17)%1024);
   b=n<100?0:(n<200?1023:(n*53+811)%1024);oa=(n%101==0);ob=(n%137==0);
   if(valid)begin
    for(i=3*R-1;i>0;i=i-1)begin ca[i]=ca[i-1];cb[i]=cb[i-1];co[i]=co[i-1];cp[i]=cp[i-1];end
    ca[0]=a;cb[0]=b;co[0]=oa;cp[0]=ob;ticks=ticks+1;phase=phase+1;
   end
   @(posedge clk);#1;
   if(valid&&phase==R)begin
    phase=0;outputs=outputs+1;
    if(outputs>3)begin
     sx=0;sy=0;eox=0;eoy=0;
     for(i=0;i<3*R;i=i+1)begin sx=sx+h[i]*ca[i];sy=sy+h[i]*cb[i];eox=eox|co[i];eoy=eoy|cp[i];end
     ex=(sx+R*R*R/2)/(R*R*R);ey=(sy+R*R*R/2)/(R*R*R);
     if(!v||x!==ex||y!==ey||ox!==eox||oy!==eoy)$fatal(1,"CIC R=%0d n=%0d got %0d/%0d/%0d/%0d v%0d expected %0d/%0d/%0d/%0d",R,n,x,y,ox,oy,v,ex,ey,eox,eoy);
    end else if(v)$fatal(1,"CIC published startup transient");
   end else if(v)$fatal(1,"CIC wrong output cadence");
  end
  @(negedge clk);rst=0;#1;if(v)$fatal(1,"CIC reset must revoke valid");
  done=1;
 end
endmodule
module tb_adc_cic;
 wire d8,d10;cic_check #(.R(8)) a(d8);cic_check #(.R(10)) b(d10);
 initial begin wait(d8&&d10);$display("ADC_CIC_PASS: independent convolution, two channels, DC endpoints, wraparound, gaps, OTR and reset");$finish;end
 initial begin #100000;$fatal(1,"CIC timeout");end
endmodule
