`timescale 1ns/1ps
// INTERNAL DIGITAL TEST ONLY: no physical ADC pins or analog measurements.
module adc_axi_capture #(
  parameter integer ADDR_BITS=12,
  parameter integer TIMEOUT_CYCLES=1000000
)(
  (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 s_axi_aclk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aclk, ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET s_axi_aresetn" *)
  input wire s_axi_aclk,
  (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 s_axi_aresetn RST", X_INTERFACE_PARAMETER="XIL_INTERFACENAME s_axi_aresetn, POLARITY ACTIVE_LOW" *)
  input wire s_axi_aresetn,
  (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 sample_clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME sample_clk, FREQ_HZ 50000000" *)
  input wire sample_clk,
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
  reg aw_have,w_have;
  reg [15:0] aw_address;
  reg [31:0] w_value;
  reg [3:0] w_strobes;
  wire write_fire=aw_have && w_have && !s_axi_bvalid;
  assign s_axi_awready=s_axi_aresetn && !aw_have && !s_axi_bvalid;
  assign s_axi_wready=s_axi_aresetn && !w_have && !s_axi_bvalid;
  wire [31:0] masked_write=w_value & {{8{w_strobes[3]}},{8{w_strobes[2]}},{8{w_strobes[1]}},{8{w_strobes[0]}}};
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
  wire [31:0] core_rd_data;
  reg core_arm;
  wire finishing=inflight && seen_busy && core_done && !core_busy;
  wire reject_event=inflight && !finishing && core_rejected;
  wire timeout_event=inflight && !finishing && !core_rejected && timer_count==TIMEOUT_CYCLES-1;
  wire memory_read=ar_accept && aligned_read && in_window && record_valid && !resetting;
  wire [31:0] status={24'b0,record_valid,1'b1,reject_flag,resetting,timeout_flag,record_error,record_done,inflight};

  (* ASYNC_REG="TRUE" *) reg [1:0] pattern_release,enable_sync;
  always @(posedge sample_clk or negedge s_axi_aresetn)
    if(!s_axi_aresetn)pattern_release<=0;else pattern_release<={pattern_release[0],1'b1};
  wire pattern_reset_n=pattern_release[1];
  reg [19:0] pattern_index;
  always @(posedge sample_clk or negedge pattern_reset_n)
    if(!pattern_reset_n)begin enable_sync<=0;pattern_index<=0;end
    else begin
      enable_sync<={enable_sync[0],test_enable};
      if(enable_sync[1])pattern_index<=pattern_index+1'b1;
    end
  wire [9:0] data1=pattern_index[9:0];
  wire [9:0] data2=(10'd1023-pattern_index[9:0]) ^ pattern_index[19:10];
  adc_snapshot_core #(.ADDR_BITS(ADDR_BITS),.WARMUP_SAMPLES(4)) snapshot(
    .ctrl_clk(s_axi_aclk),.sample_clk(sample_clk),.reset_n(core_reset_n),
    .arm(core_arm),.busy(core_busy),.done(core_done),.error(core_error),.rejected(core_rejected),
    .rd_en(memory_read),.rd_addr(s_axi_araddr[ADDR_BITS+1:2]),.rd_valid(core_rd_valid),.rd_data(core_rd_data),
    .sample_ready(pattern_reset_n),.sample_valid(pattern_reset_n && enable_sync[1]),
    .data1(data1),.data2(data2),.otr1(pattern_index[2]),.otr2(pattern_index[3]));

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
            16'h0004:s_axi_rdata<=32'h00010000;
            16'h0008:s_axi_rdata<=0;
            16'h000c:s_axi_rdata<=status;
            16'h0010:s_axi_rdata<=DEPTH;
            16'h0014:s_axi_rdata<=DEPTH*4;
            16'h0018:s_axi_rdata<=capture_id;
            16'h001c:s_axi_rdata<=TIMEOUT_CYCLES;
            16'h0020:s_axi_rdata<={31'b0,test_enable};
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
      reset_hold<=8;reset_settle<=8;
    end else begin
      core_arm<=0;
      if(reset_hold!=0)reset_hold<=reset_hold-1'b1;
      else if(reset_settle!=0)reset_settle<=reset_settle-1'b1;
      if(s_axi_awready && s_axi_awvalid)begin aw_have<=1;aw_address<=s_axi_awaddr;end
      if(s_axi_wready && s_axi_wvalid)begin w_have<=1;w_value<=s_axi_wdata;w_strobes<=s_axi_wstrb;end
      if(s_axi_bvalid && s_axi_bready)s_axi_bvalid<=0;
      if(inflight)begin
        timer_count<=timer_count+1'b1;
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
            1:if(inflight || resetting || read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
              else begin core_arm<=1;inflight<=1;seen_busy<=0;timer_count<=0;
                record_done<=0;record_error<=0;record_valid<=0;end
            2:if(read_pending || s_axi_rvalid)begin s_axi_bresp<=2;reject_flag<=1;end
              else begin inflight<=0;seen_busy<=0;record_done<=0;record_error<=0;record_valid<=0;
                timeout_flag<=0;reject_flag<=0;reset_hold<=8;reset_settle<=8;end
            4:begin timeout_flag<=timeout_event;reject_flag<=reject_event;end
            default:s_axi_bresp<=2;
          endcase
          16'h0020:if((masked_write & 32'hfffffffe)!=0)s_axi_bresp<=2;
            else if(w_strobes[0])test_enable<=masked_write[0];
          default:s_axi_bresp<=2;
        endcase
      end
    end
  end
endmodule
