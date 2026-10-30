`timescale 1ns/1ps
// Frozen dual-channel ring; only a complete record grants control-domain reads.
// Trigger config is held by the caller from before ARM through completion.
module adc_trigger_ring_core #(
 parameter integer ADDR_BITS=12,parameter integer WARMUP_SAMPLES=4
)(
 input wire ctrl_clk,sample_clk,reset_n,arm,
 output reg busy,done,error,rejected,
 input wire rd_en,input wire [ADDR_BITS-1:0] rd_addr,
 output reg rd_valid,output reg [31:0] rd_data,
 input wire sample_ready,sample_valid,input wire [9:0] data1,data2,input wire otr1,otr2,
 input wire cfg_enable,cfg_channel,cfg_rising,cfg_wait_only,
 input wire [17:0] cfg_level_q8,input wire [ADDR_BITS:0] cfg_pre_samples,
 input wire [9:0] cfg_hysteresis,
 output wire waiting,output reg [31:0] progress,
 output reg matched,auto_forced,output reg [ADDR_BITS:0] trigger_after,
 output reg [ADDR_BITS-1:0] record_start,output reg [31:0] samples_seen
);
 localparam integer DEPTH=1<<ADDR_BITS;
 localparam [1:0] WAIT_REQUEST=0,FILL=1,POST=2,COMPLETE=3;
 (* ram_style="block" *) reg [31:0] memory[0:DEPTH-1];
 (* ASYNC_REG="TRUE" *) reg [1:0] ctrl_release,sample_release;
 always @(posedge ctrl_clk or negedge reset_n)
  if(!reset_n)ctrl_release<=0;else ctrl_release<={ctrl_release[0],1'b1};
 always @(posedge sample_clk or negedge reset_n)
  if(!reset_n)sample_release<=0;else sample_release<={sample_release[0],1'b1};
 wire ctrl_reset_n=ctrl_release[1],sample_reset_n=sample_release[1];
 reg request_toggle,completion_pending;
 (* ASYNC_REG="TRUE" *) reg [1:0] request_sync,complete_sync,error_sync,wait_sync,progress_sync;
 reg handled_request,complete_toggle,result_error,progress_toggle;
 reg [1:0] state;
 reg frozen_matched_s,frozen_auto_s;
 reg [ADDR_BITS:0] frozen_after_s;
 reg [ADDR_BITS-1:0] frozen_start_s;
 reg [31:0] frozen_seen_s;
 reg active_enable,active_channel,active_rising,active_wait;
 wire waiting_s=state==FILL && active_enable;
 always @(posedge sample_clk or negedge sample_reset_n)
  if(!sample_reset_n)request_sync<=0;else request_sync<={request_sync[0],request_toggle};
 always @(posedge ctrl_clk or negedge ctrl_reset_n)
  if(!ctrl_reset_n)begin complete_sync<=0;error_sync<=0;wait_sync<=0;progress_sync<=0;end
  else begin
   complete_sync<={complete_sync[0],complete_toggle};error_sync<={error_sync[0],result_error};
   wait_sync<={wait_sync[0],waiting_s};progress_sync<={progress_sync[0],progress_toggle};
  end
 assign waiting=wait_sync[1];
 reg progress_seen;
 always @(posedge ctrl_clk or negedge ctrl_reset_n)begin
  if(!ctrl_reset_n)begin
   request_toggle<=0;completion_pending<=0;busy<=0;done<=0;error<=0;rejected<=0;rd_valid<=0;
   progress<=0;progress_seen<=0;matched<=0;auto_forced<=0;trigger_after<=0;record_start<=0;samples_seen<=0;
  end else begin
   rejected<=arm&&busy;rd_valid<=rd_en&&done&&!error&&!arm;
   if(progress_seen!=progress_sync[1])begin progress<=progress+1'b1;progress_seen<=progress_sync[1];end
   if(arm&&!busy)begin
    request_toggle<=~request_toggle;completion_pending<=0;busy<=1;done<=0;error<=0;
    matched<=0;auto_forced<=0;trigger_after<=0;record_start<=0;samples_seen<=0;
   end else if(busy&&complete_sync[1]==request_toggle)begin
    // Frozen metadata predates the completion toggle by one sample clock.
    if(!completion_pending)completion_pending<=1;
    else begin
     busy<=0;done<=1;error<=error_sync[1];matched<=frozen_matched_s;auto_forced<=frozen_auto_s;
     trigger_after<=frozen_after_s;record_start<=frozen_start_s;samples_seen<=frozen_seen_s;
    end
   end
  end
 end
 wire [ADDR_BITS-1:0] physical_read=rd_addr+record_start;
 always @(posedge ctrl_clk)
  if(ctrl_reset_n&&rd_en&&done&&!error&&!arm)rd_data<=memory[physical_read];

 reg [17:0] active_level;
 reg [ADDR_BITS:0] active_pre,filled_count,post_left;
 reg [9:0] active_hysteresis,previous_code;
 reg previous_valid,edge_armed;
 reg [ADDR_BITS-1:0] write_address;
 reg [31:0] warmup_left,seen_count;
 wire [31:0] next_seen=(&seen_count)?seen_count:seen_count+1'b1;
 wire [9:0] selected_code=active_channel?data2:data1;
 wire selected_otr=active_channel?otr2:otr1;
 wire [17:0] code_q8={selected_code,8'b0},previous_q8={previous_code,8'b0};
 wire [17:0] hysteresis_q8={active_hysteresis,8'b0};
 wire [17:0] low_q8=active_level>hysteresis_q8?active_level-hysteresis_q8:18'b0;
 wire [18:0] sum_high={1'b0,active_level}+{1'b0,hysteresis_q8};
 wire [17:0] high_q8=sum_high>19'd261888 ? 18'd261888 : sum_high[17:0];
 wire crossing=previous_valid&&edge_armed&&!selected_otr&&
  (active_rising?(previous_q8<=active_level&&code_q8>active_level):(previous_q8>=active_level&&code_q8<active_level));
 wire write_sample=(state==FILL||state==POST)&&sample_ready&&sample_valid&&warmup_left==0;
 always @(posedge sample_clk)
  if(sample_reset_n&&write_sample)memory[write_address]<={5'b0,otr2,data2,5'b0,otr1,data1};
 always @(posedge sample_clk or negedge sample_reset_n)begin
  if(!sample_reset_n)begin
   state<=WAIT_REQUEST;handled_request<=0;complete_toggle<=0;result_error<=0;progress_toggle<=0;
   frozen_matched_s<=0;frozen_auto_s<=0;frozen_after_s<=0;frozen_start_s<=0;frozen_seen_s<=0;
   active_enable<=0;active_channel<=0;active_rising<=0;active_wait<=0;active_level<=0;active_pre<=1;active_hysteresis<=1;
   filled_count<=0;post_left<=0;previous_code<=0;previous_valid<=0;edge_armed<=0;write_address<=0;warmup_left<=0;seen_count<=0;
  end else begin
   if((state==FILL||state==POST)&&sample_ready&&sample_valid)progress_toggle<=~progress_toggle;
   case(state)
    WAIT_REQUEST:if(request_sync[1]!=handled_request)begin
     handled_request<=request_sync[1];result_error<=0;write_address<=0;filled_count<=0;seen_count<=0;
     previous_valid<=0;edge_armed<=0;warmup_left<=WARMUP_SAMPLES;post_left<=0;
     frozen_matched_s<=0;frozen_auto_s<=0;frozen_after_s<=0;frozen_start_s<=0;frozen_seen_s<=0;
     active_enable<=cfg_enable;active_channel<=cfg_channel;active_rising<=cfg_rising;active_wait<=cfg_wait_only;
     active_level<=cfg_level_q8;active_pre<=cfg_pre_samples;active_hysteresis<=cfg_hysteresis;
     if(cfg_enable&&(cfg_level_q8>18'd261888||cfg_pre_samples<1||cfg_pre_samples>DEPTH-2||cfg_hysteresis==0))begin result_error<=1;state<=COMPLETE;end
     else state<=FILL;
    end
    FILL,POST:begin
     if(!sample_ready)begin result_error<=1;state<=COMPLETE;end
     else if(sample_valid)begin
      if(warmup_left!=0)warmup_left<=warmup_left-1'b1;
      else begin
       seen_count<=next_seen;write_address<=write_address+1'b1;
       if(filled_count<DEPTH)filled_count<=filled_count+1'b1;
       if(state==POST)begin
        if(post_left==1)begin
         frozen_start_s<=write_address+1'b1;frozen_seen_s<=next_seen;state<=COMPLETE;
        end else post_left<=post_left-1'b1;
       end else begin
        previous_code<=selected_code;previous_valid<=!selected_otr;
        if(selected_otr)edge_armed<=0;
        else if(active_rising?code_q8<=low_q8:code_q8>=high_q8)edge_armed<=1;
        if(crossing)edge_armed<=0;
        if(active_enable&&crossing&&filled_count>=active_pre)begin
         frozen_matched_s<=1;frozen_auto_s<=0;frozen_after_s<=active_pre;
         post_left<=DEPTH-1-active_pre;state<=POST;
        end else if(filled_count==DEPTH-1&&(!active_enable||!active_wait))begin
         frozen_matched_s<=0;frozen_auto_s<=active_enable;frozen_after_s<=0;
         frozen_start_s<=write_address+1'b1;frozen_seen_s<=next_seen;state<=COMPLETE;
        end
       end
      end
     end
    end
    COMPLETE:begin complete_toggle<=handled_request;state<=WAIT_REQUEST;end
    default:begin result_error<=1;state<=COMPLETE;end
   endcase
  end
 end
endmodule
