`timescale 1ns/1ps
// External 3PA1030 input only; the frontend must meet the sample clock timing.
module adc_live_axi #(
  parameter integer ADDR_BITS=12,
  parameter integer TIMEOUT_CYCLES=1000000,
  parameter integer CTRL_HZ=100000000,
  parameter integer CONVERSION_HZ=5000000
)(
  (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 s_axi_aclk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aclk, ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET s_axi_aresetn" *)
  input wire s_axi_aclk,
  (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 s_axi_aresetn RST", X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aresetn, POLARITY ACTIVE_LOW" *)
  input wire s_axi_aresetn,
  (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 sample_clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME sample_clk, FREQ_HZ 50000000" *)
  input wire sample_clk,
  input wire input_ready,input wire input_valid,
  input wire raw_valid,input wire [9:0] raw_data1,raw_data2,input wire raw_otr1,raw_otr2,
  input wire [9:0] adc_data1,adc_data2,
  input wire adc_otr1,adc_otr2,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWADDR", X_INTERFACE_PARAMETER="XIL_INTERFACENAME S_AXI, PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 16, READ_WRITE_MODE READ_WRITE, HAS_BURST 0, HAS_LOCK 0, NUM_READ_OUTSTANDING 1, NUM_WRITE_OUTSTANDING 1" *)
  input wire [15:0] s_axi_awaddr,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *) input wire [2:0] s_axi_awprot,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *) input wire s_axi_awvalid,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *) output wire s_axi_awready,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WDATA" *) input wire [31:0] s_axi_wdata,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *) input wire [3:0] s_axi_wstrb,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WVALID" *) input wire s_axi_wvalid,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI WREADY" *) output wire s_axi_wready,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BRESP" *) output reg [1:0] s_axi_bresp,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BVALID" *) output reg s_axi_bvalid,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI BREADY" *) input wire s_axi_bready,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *) input wire [15:0] s_axi_araddr,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *) input wire [2:0] s_axi_arprot,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *) input wire s_axi_arvalid,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *) output wire s_axi_arready,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RDATA" *) output reg [31:0] s_axi_rdata,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RRESP" *) output reg [1:0] s_axi_rresp,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RVALID" *) output reg s_axi_rvalid,
  (* X_INTERFACE_INFO="xilinx.com:interface:aximm:1.0 S_AXI RREADY" *) input wire s_axi_rready
);
  localparam integer DEPTH=1<<ADDR_BITS;
  localparam integer HIGH_PLATFORM=(CONVERSION_HZ>=25000000);
  localparam integer PIPE_NORM=(CONVERSION_HZ>=50000000);
  reg aw_have,w_have;
  reg [15:0] aw_address;
  reg [31:0] w_value;
  reg [3:0] w_strobes;
  wire write_fire=aw_have && w_have && !s_axi_bvalid;
  assign s_axi_awready=s_axi_aresetn && !aw_have && !s_axi_bvalid;
  assign s_axi_wready=s_axi_aresetn && !w_have && !s_axi_bvalid;
  wire [31:0] strobe_mask= {{8{w_strobes[3]}},{8{w_strobes[2]}},{8{w_strobes[1]}},{8{w_strobes[0]}}};
  wire [31:0] masked_write=w_value & strobe_mask;
  reg read_pending;
  assign s_axi_arready=s_axi_aresetn && !s_axi_rvalid && !read_pending && !write_fire;
  wire ar_accept=s_axi_arready && s_axi_arvalid;
  wire aligned_read=s_axi_araddr[1:0]==0;
  wire in_window=s_axi_araddr>=16'h4000 && s_axi_araddr<(32'h4000+DEPTH*4);
  reg record_done,record_error,record_valid,inflight,seen_busy;
  reg timeout_flag,reject_flag,test_enable;
  reg [31:0] capture_id,timer_count;
  reg [3:0] reset_hold,reset_settle;
  wire resetting=(reset_hold!=0 || reset_settle!=0);
  wire core_reset_n=s_axi_aresetn && reset_hold==0;
  wire core_busy,core_done,core_error,core_rejected,core_rd_valid;
  wire [31:0] core_rd_data,core_progress,core_seen;
  wire core_waiting,core_matched,core_auto;
  wire [ADDR_BITS:0] core_after;
  wire [ADDR_BITS-1:0] core_start;
  reg [31:0] progress_last,cfg_trigger_control,cfg_trigger_level,cfg_trigger_pre,cfg_trigger_hysteresis;
  wire [31:0] merged_control=(cfg_trigger_control & ~strobe_mask)|masked_write;
  wire [31:0] merged_level=(cfg_trigger_level & ~strobe_mask)|masked_write;
  wire [31:0] merged_pre=(cfg_trigger_pre & ~strobe_mask)|masked_write;
  wire [31:0] merged_hysteresis=(cfg_trigger_hysteresis & ~strobe_mask)|masked_write;
  reg core_arm;
  wire finishing=inflight && seen_busy && core_done && !core_busy;
  wire reject_event=inflight && !finishing && core_rejected;
  wire [31:0] watchdog_limit;
  wire timeout_event=inflight && !finishing && !core_rejected && timer_count==watchdog_limit-1 && core_progress==progress_last;
  wire memory_read=ar_accept && aligned_read && in_window && record_valid && !resetting;
  wire [31:0] status={24'b0,record_valid,1'b0,reject_flag,resetting,timeout_flag,record_error,record_done,inflight};

  (* ASYNC_REG="TRUE" *) reg [1:0] pattern_release,enable_sync,ready_sync;
  always @(posedge s_axi_aclk or negedge s_axi_aresetn)
    if(!s_axi_aresetn)ready_sync<=0;else ready_sync<={ready_sync[0],input_ready};
  always @(posedge sample_clk or negedge s_axi_aresetn)
    if(!s_axi_aresetn)pattern_release<=0;else pattern_release<={pattern_release[0],1'b1};
  wire pattern_reset_n=pattern_release[1];
  always @(posedge sample_clk or negedge pattern_reset_n)
    if(!pattern_reset_n)enable_sync<=0;else enable_sync<={enable_sync[0],test_enable};
  // Request payload stays stable until the sample-domain acknowledgement.
  reg [2:0] rate_request,rate_applied,rate_sample;
  reg config_toggle,config_ack,config_settle;
  (* ASYNC_REG="TRUE" *) reg [1:0] config_req_sync,config_ack_sync;
  wire config_busy=config_toggle!=config_ack_sync[1];
  wire [31:0] record_rate=rate_applied==4?CONVERSION_HZ:rate_applied==5?6250000:rate_applied==0?625000:rate_applied==1?62500:rate_applied==2?6250:625;
  localparam [31:0] LIMIT1=(64'd1*CTRL_HZ*(DEPTH+16))/62500+CTRL_HZ/20;
  localparam [31:0] LIMIT2=(64'd1*CTRL_HZ*(DEPTH+16))/6250+CTRL_HZ/20;
  localparam [31:0] LIMIT3=(64'd1*CTRL_HZ*(DEPTH+16))/625+CTRL_HZ/20;
  assign watchdog_limit=(rate_applied==4||rate_applied==5)?TIMEOUT_CYCLES:rate_applied==0?TIMEOUT_CYCLES:rate_applied==1?LIMIT1:rate_applied==2?LIMIT2:LIMIT3;
  always @(posedge sample_clk or negedge pattern_reset_n)
    if(!pattern_reset_n)begin config_req_sync<=0;config_ack<=0;config_settle<=0;rate_sample<=0;end
    else begin
      config_req_sync<={config_req_sync[0],config_toggle};
      if(config_req_sync[1]!=config_ack)begin
        if(!config_settle)config_settle<=1;
        else begin rate_sample<=rate_request;config_ack<=config_req_sync[1];config_settle<=0;end
      end else config_settle<=0;
    end
  always @(posedge s_axi_aclk or negedge s_axi_aresetn)
    if(!s_axi_aresetn)begin config_ack_sync<=0;rate_applied<=0;end
    else begin config_ack_sync<={config_ack_sync[0],config_ack};if(!config_busy)rate_applied<=rate_request;end
  wire v1,v2,v3;wire [9:0] a1,b1,a2,b2,a3,b3;wire o11,o12,o21,o22,o31,o32;
  wire base_valid,base_otr1,base_otr2;wire [9:0] base1,base2;
  generate if(HIGH_PLATFORM)begin: high_base
   adc_cic_decimator #(.R(10),.PIPELINED_OUTPUT(PIPE_NORM)) filter0(sample_clk,pattern_reset_n,input_ready&&input_valid,adc_data1,adc_data2,adc_otr1,adc_otr2,base_valid,base1,base2,base_otr1,base_otr2);
  end else begin: legacy_base
   assign base_valid=input_valid;assign base1=adc_data1;assign base2=adc_data2;assign base_otr1=adc_otr1;assign base_otr2=adc_otr2;
  end endgenerate
  adc_cic_decimator #(.R(10),.PIPELINED_OUTPUT(PIPE_NORM)) filter1(sample_clk,pattern_reset_n,input_ready&&base_valid,base1,base2,base_otr1,base_otr2,v1,a1,b1,o11,o12);
  adc_cic_decimator #(.R(10),.PIPELINED_OUTPUT(PIPE_NORM)) filter2(sample_clk,pattern_reset_n,v1,a1,b1,o11,o12,v2,a2,b2,o21,o22);
  adc_cic_decimator #(.R(10),.PIPELINED_OUTPUT(PIPE_NORM)) filter3(sample_clk,pattern_reset_n,v2,a2,b2,o21,o22,v3,a3,b3,o31,o32);
  wire chosen_valid=rate_sample==4?raw_valid:rate_sample==5?input_valid:rate_sample==0?base_valid:rate_sample==1?v1:rate_sample==2?v2:v3;
  wire [9:0] chosen1=rate_sample==4?raw_data1:rate_sample==5?adc_data1:rate_sample==0?base1:rate_sample==1?a1:rate_sample==2?a2:a3;
  wire [9:0] chosen2=rate_sample==4?raw_data2:rate_sample==5?adc_data2:rate_sample==0?base2:rate_sample==1?b1:rate_sample==2?b2:b3;
  wire chosen_otr1=rate_sample==4?raw_otr1:rate_sample==5?adc_otr1:rate_sample==0?base_otr1:rate_sample==1?o11:rate_sample==2?o21:o31;
  wire chosen_otr2=rate_sample==4?raw_otr2:rate_sample==5?adc_otr2:rate_sample==0?base_otr2:rate_sample==1?o12:rate_sample==2?o22:o32;
  adc_trigger_ring_core #(.ADDR_BITS(ADDR_BITS),.WARMUP_SAMPLES(4)) snapshot(
    .ctrl_clk(s_axi_aclk),.sample_clk(sample_clk),.reset_n(core_reset_n),
    .arm(core_arm),.busy(core_busy),.done(core_done),.error(core_error),.rejected(core_rejected),
    .rd_en(memory_read),.rd_addr(s_axi_araddr[ADDR_BITS+1:2]),.rd_valid(core_rd_valid),.rd_data(core_rd_data),
    .sample_ready(pattern_reset_n && input_ready),.sample_valid(pattern_reset_n && input_ready && chosen_valid && enable_sync[1]),
    .data1(chosen1),.data2(chosen2),.otr1(chosen_otr1),.otr2(chosen_otr2),
    .cfg_enable(cfg_trigger_control[0]),.cfg_channel(cfg_trigger_control[1]),.cfg_rising(cfg_trigger_control[2]),.cfg_wait_only(cfg_trigger_control[3]),
    .cfg_level_q8(cfg_trigger_level[17:0]),.cfg_pre_samples(cfg_trigger_pre[ADDR_BITS:0]),.cfg_hysteresis(cfg_trigger_hysteresis[9:0]),
    .waiting(core_waiting),.progress(core_progress),.matched(core_matched),.auto_forced(core_auto),
    .trigger_after(core_after),.record_start(core_start),.samples_seen(core_seen));

  always @(posedge s_axi_aclk or negedge s_axi_aresetn)begin
    if(!s_axi_aresetn)begin
      s_axi_rvalid<=0;s_axi_rresp<=0;s_axi_rdata<=0;read_pending<=0;
    end else begin
      if(s_axi_rvalid && s_axi_rready)s_axi_rvalid<=0;
      if(read_pending && core_rd_valid)begin
        s_axi_rdata<=core_rd_data;s_axi_rresp<=0;s_axi_rvalid<=1;read_pending<=0;
      end else if(ar_accept)begin
        s_axi_rresp<=0;s_axi_rdata<=0;
        if(!aligned_read)begin s_axi_rresp<=2;s_axi_rvalid<=1;end
        else if(in_window)begin
          if(memory_read)read_pending<=1;
          else begin s_axi_rresp<=2;s_axi_rvalid<=1;end
        end else begin
          s_axi_rvalid<=1;
          case(s_axi_araddr)
            16'h0000:s_axi_rdata<=32'h53414443;
            16'h0004:s_axi_rdata<=CONVERSION_HZ>=50000000?32'h00070000:HIGH_PLATFORM?32'h00060000:32'h00050000;
            16'h0008:s_axi_rdata<=0;
            16'h000c:s_axi_rdata<=status;
            16'h0010:s_axi_rdata<=DEPTH;
            16'h0014:s_axi_rdata<=DEPTH*4;
            16'h0018:s_axi_rdata<=capture_id;
            16'h001c:s_axi_rdata<=watchdog_limit;
            16'h0020:s_axi_rdata<={31'b0,test_enable};
            16'h0024:s_axi_rdata<=record_rate;
            16'h002c:s_axi_rdata<=CONVERSION_HZ;
            16'h0028:s_axi_rdata<={31'b0,ready_sync[1]};
            16'h0030:s_axi_rdata<={29'b0,rate_request};
            16'h0034:s_axi_rdata<={21'b0,rate_applied,7'b0,config_busy};
            16'h003c:s_axi_rdata<=rate_applied==4?0:3;
            16'h0040:s_axi_rdata<=cfg_trigger_control;
            16'h0044:s_axi_rdata<=cfg_trigger_level;
            16'h0048:s_axi_rdata<=cfg_trigger_pre;
            16'h004c:s_axi_rdata<=cfg_trigger_hysteresis;
            16'h0054:s_axi_rdata<={23'b0,core_waiting,6'b0,core_auto,core_matched};
            16'h0058:s_axi_rdata<=core_after;
            16'h005c:s_axi_rdata<=core_progress;
            16'h0064:s_axi_rdata<=core_start;
            16'h0068:s_axi_rdata<=core_seen;
            default:s_axi_rresp<=2;
          endcase
        end
      end
    end
  end

  always @(posedge s_axi_aclk or negedge s_axi_aresetn)begin
    if(!s_axi_aresetn)begin
      aw_have<=0;w_have<=0;aw_address<=0;w_value<=0;w_strobes<=0;
      s_axi_bvalid<=0;s_axi_bresp<=0;core_arm<=0;
      inflight<=0;seen_busy<=0;record_done<=0;record_error<=0;record_valid<=0;
      timeout_flag<=0;reject_flag<=0;test_enable<=1;capture_id<=0;timer_count<=0;
      reset_hold<=8;reset_settle<=8;rate_request<=0;config_toggle<=0;
      cfg_trigger_control<=0;cfg_trigger_level<=128000;cfg_trigger_pre<=1;cfg_trigger_hysteresis<=10;progress_last<=0;
    end else begin
      core_arm<=0;
      if(reset_hold!=0)reset_hold<=reset_hold-1'b1;
      else if(reset_settle!=0)reset_settle<=reset_settle-1'b1;
      if(s_axi_awready && s_axi_awvalid)begin aw_have<=1;aw_address<=s_axi_awaddr;end
      if(s_axi_wready && s_axi_wvalid)begin w_have<=1;w_value<=s_axi_wdata;w_strobes<=s_axi_wstrb;end
      if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
      if(inflight)begin
        if(core_progress!=progress_last)begin timer_count<=0;progress_last<=core_progress;end
        else timer_count<=timer_count+1'b1;
        if(core_busy)seen_busy<=1;
        if(seen_busy && core_done && !core_busy)begin
          inflight<=0;record_done<=1;record_error<=core_error;record_valid<=!core_error;
          if(!core_error)capture_id<=capture_id+1'b1;
        end else if(timeout_event || reject_event)begin
          inflight<=0;record_done<=1;record_error<=1;record_valid<=0;
          if(timeout_event)timeout_flag<=1;
          if(reject_event)reject_flag<=1;
          reset_hold<=8;reset_settle<=8;
        end
      end
      if(write_fire)begin
        aw_have<=0;w_have<=0;s_axi_bvalid<=1;s_axi_bresp<=0;
        if(aw_address[1:0]!=0)s_axi_bresp<=2;
        else case(aw_address)
          16'h0008:case(masked_write)
            0:begin end
            1:if(inflight || resetting || config_busy || rate_applied!=rate_request || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
              else begin core_arm<=1;inflight<=1;seen_busy<=0;timer_count<=0;progress_last<=core_progress;
                record_done<=0;record_error<=0;record_valid<=0;end
            2:if(read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
              else begin inflight<=0;seen_busy<=0;record_done<=0;record_error<=0;record_valid<=0;
                timeout_flag<=0;reject_flag<=0;reset_hold<=8;reset_settle<=8;end
            4:begin timeout_flag<=timeout_event;reject_flag<=reject_event;end
            default:s_axi_bresp<=2;
          endcase
          16'h0020:if((masked_write & 32'hfffffffe)!=0)s_axi_bresp<=2;
            else if(w_strobes[0])test_enable<=masked_write[0];
          16'h0030:if(masked_write>(HIGH_PLATFORM?5:4))s_axi_bresp<=2;
            else if(inflight || resetting || config_busy || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
            else if(w_strobes[0] && rate_request!=masked_write[2:0])begin
              rate_request<=masked_write[2:0];config_toggle<=~config_toggle;
              record_done<=0;record_valid<=0;record_error<=0;
            end
          16'h0040:if(inflight || resetting || config_busy || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
            else if(merged_control>15)s_axi_bresp<=2;
            else if(cfg_trigger_control!=merged_control)begin cfg_trigger_control<=merged_control;record_done<=0;record_valid<=0;record_error<=0;end
          16'h0044:if(inflight || resetting || config_busy || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
            else if(merged_level>261888)s_axi_bresp<=2;
            else if(cfg_trigger_level!=merged_level)begin cfg_trigger_level<=merged_level;record_done<=0;record_valid<=0;record_error<=0;end
          16'h0048:if(inflight || resetting || config_busy || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
            else if(merged_pre<1 || merged_pre>DEPTH-2)s_axi_bresp<=2;
            else if(cfg_trigger_pre!=merged_pre)begin cfg_trigger_pre<=merged_pre;record_done<=0;record_valid<=0;record_error<=0;end
          16'h004c:if(inflight || resetting || config_busy || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
            else if(merged_hysteresis<1 || merged_hysteresis>1023)s_axi_bresp<=2;
            else if(cfg_trigger_hysteresis!=merged_hysteresis)begin cfg_trigger_hysteresis<=merged_hysteresis;record_done<=0;record_valid<=0;record_error<=0;end
          default:s_axi_bresp<=2;
        endcase
      end
    end
  end
endmodule
