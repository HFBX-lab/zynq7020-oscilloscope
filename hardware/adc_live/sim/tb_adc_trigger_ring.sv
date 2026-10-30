`timescale 1ns/1ps
module tb_adc_trigger_ring;
 parameter ADDR_BITS=4;localparam DEPTH=1<<ADDR_BITS;
 reg cc=0,sc=0,rst=0;always #5 cc=~cc;always #14 sc=~sc;
 reg arm=0,rd=0,sv=0,ready=1;reg [ADDR_BITS-1:0] ra=0;
 reg [9:0] a=0,b=0;reg oa=0,ob=0;
 reg en=1,ch=1,rise=1,wait_only=1;
 reg [17:0] level=500*256;reg [ADDR_BITS:0] pre=4;reg [9:0] hyst=10;
 wire busy,done,error,reject,rv,waiting,matched,forced;wire [31:0] data,progress,seen;
 wire [ADDR_BITS:0] after_index;wire [ADDR_BITS-1:0] start;
 adc_trigger_ring_core #(.ADDR_BITS(ADDR_BITS)) dut(
 .ctrl_clk(cc),.sample_clk(sc),.reset_n(rst),.arm(arm),.busy(busy),.done(done),.error(error),.rejected(reject),
 .rd_en(rd),.rd_addr(ra),.rd_valid(rv),.rd_data(data),.sample_ready(ready),.sample_valid(sv),.data1(a),.data2(b),.otr1(oa),.otr2(ob),
 .cfg_enable(en),.cfg_channel(ch),.cfg_rising(rise),.cfg_wait_only(wait_only),.cfg_level_q8(level),.cfg_pre_samples(pre),.cfg_hysteresis(hyst),
 .waiting(waiting),.progress(progress),.matched(matched),.auto_forced(forced),.trigger_after(after_index),.record_start(start),.samples_seen(seen));
 integer tag=0,edge_tag,i,first_tag;reg [31:0] held;
 task tick;@(posedge cc);#1;endtask
 task arm_it;begin @(negedge cc);arm=1;tick();@(negedge cc);arm=0;repeat(8)@(negedge sc);end endtask
 task push(input integer signal_code,input integer bad);begin
  @(negedge sc);sv=1;
  if(ch)begin a=tag&1023;b=signal_code;oa=0;ob=bad;end
  else begin a=signal_code;b=tag&1023;oa=bad;ob=0;end
  tag=tag+1;@(posedge sc);#1;@(negedge sc);sv=0;
 end endtask
 task get(input integer address);begin
  @(negedge cc);rd=1;ra=address;tick();if(!rv)$fatal(1,"read not valid");held=data;@(negedge cc);rd=0;
 end endtask
 task finish_check(input integer expected_match,input integer expected_auto);begin
  repeat(20)tick();if(!done||busy||error||matched!==expected_match[0]||forced!==expected_auto[0])$fatal(1,"completion flags");
 end endtask
 initial begin
  #1;rst=1;#2;rst=0;#123;rst=1;repeat(20)tick();
  arm_it();for(i=0;i<3*DEPTH+4;i=i+1)push(100+((tag>>10)&15),0);
  if(done||!busy||!waiting||progress<DEPTH)$fatal(1,"NORMAL did not keep waiting across rings");
  push(499,0);edge_tag=tag;push(501,0);
  // Never reach510: trigger must be immediate at theactual level crossing.
  for(i=0;i<DEPTH-1-pre;i=i+1)push(DEPTH>16?600+((tag>>10)&15):501,0);
  finish_check(1,0);if(after_index!=pre||seen<3*DEPTH||start==0)$fatal(1,"ring metadata");
  first_tag=edge_tag-pre;
  for(i=0;i<DEPTH;i=i+1)begin get(i);if(DEPTH>16&&i>pre&&held[25:16]!=(600+(((first_tag+i)>>10)&15)))$fatal(1,"high address bits lost in ring %0d",i);if(held[9:0]!==((first_tag+i)&1023)||held[31:27]||held[15:11])$fatal(1,"ring logical order %0d %h",i,held);end
  get(pre-1);if(held[25:16]!=499)$fatal(1,"before edge");get(pre);if(held[25:16]!=501)$fatal(1,"after edge");
  for(i=0;i<DEPTH+4;i=i+1)push(800,0);get(0);if(held[9:0]!=(first_tag&1023))$fatal(1,"frozen record overwritten");
  // OTR disconnects continuity. A later genuine edge must still work.
  arm_it();for(i=0;i<DEPTH+4;i=i+1)push(100,0);push(499,1);push(501,0);
  for(i=0;i<DEPTH;i=i+1)push(501,0);if(done)$fatal(1,"OTR discontinuity triggered");
  push(100,0);push(499,0);push(501,0);for(i=0;i<DEPTH-1-pre;i=i+1)push(501,0);finish_check(1,0);
  // Falling CH1 with theother channeltagged independently.
  ch=0;rise=0;arm_it();for(i=0;i<DEPTH+4;i=i+1)push(900,0);
  push(501,0);edge_tag=tag;push(499,0);for(i=0;i<DEPTH-1-pre;i=i+1)push(499,0);finish_check(1,0);
  for(i=0;i<DEPTH;i=i+1)begin get(i);if(held[25:16]!==((edge_tag-pre+i)&1023))$fatal(1,"falling ring order");end
  wait_only=0;arm_it();for(i=0;i<DEPTH+4;i=i+1)push(900,0);finish_check(0,1);if(start!=0||seen!=DEPTH)$fatal(1,"AUTO full record metadata");
  // Reset cancels a pending ring and all publication ownership.
  wait_only=1;arm_it();for(i=0;i<DEPTH;i=i+1)push(900,0);
  rst=0;#1;if(busy||done||rv)$fatal(1,"reset retained ownership");rst=1;repeat(20)tick();
  en=0;arm_it();for(i=0;i<DEPTH+4;i=i+1)push(123,0);finish_check(0,0);
  for(i=0;i<DEPTH;i=i+1)begin get(i);if(held[9:0]!=123)$fatal(1,"raw snapshot compatibility");end
  $display("ADC_RING_PASS: NORMAL multiple wraps, immediate Schmitt edge, two channels/directions, OTR, logical order, AUTO, freeze, reset, raw compatibility");$finish;
 end
 initial begin #10000000;$fatal(1,"ring global timeout");end
endmodule
