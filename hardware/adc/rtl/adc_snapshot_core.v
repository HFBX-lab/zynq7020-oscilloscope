`timescale 1ns/1ps
// Fixed-length dual ADC snapshot. This is a DIGITAL acquisition core, not an
// external-pin timing solution. Data/OTR must already meet sample_clk timing.
module adc_snapshot_core #(
  parameter integer ADDR_BITS=12,
  parameter integer WARMUP_SAMPLES=4
)(
  input wire ctrl_clk,input wire sample_clk,input wire reset_n,
  input wire arm,output reg busy,output reg done,output reg error,
  output reg rejected,
  input wire rd_en,input wire [ADDR_BITS-1:0] rd_addr,
  output reg rd_valid,output reg [31:0] rd_data,
  input wire sample_ready,input wire sample_valid,
  input wire [9:0] data1,input wire [9:0] data2,
  input wire otr1,input wire otr2
);
  localparam integer DEPTH=1<<ADDR_BITS;
  localparam [1:0] WAIT_REQUEST=0,CAPTURE=1,COMPLETE=2;
  (* ram_style="block" *) reg [31:0] memory [0:DEPTH-1];
  (* ASYNC_REG="TRUE" *) reg [1:0] ctrl_release,sample_release;
  always @(posedge ctrl_clk or negedge reset_n)
    if(!reset_n)ctrl_release<=0;else ctrl_release<={ctrl_release[0],1'b1};
  always @(posedge sample_clk or negedge reset_n)
    if(!reset_n)sample_release<=0;else sample_release<={sample_release[0],1'b1};
  wire ctrl_reset_n=ctrl_release[1];
  wire sample_reset_n=sample_release[1];

  reg request_toggle,completion_pending;
  (* ASYNC_REG="TRUE" *) reg [1:0] request_sync;
  reg handled_request,complete_toggle,result_error;
  (* ASYNC_REG="TRUE" *) reg [1:0] complete_sync,error_sync;
  always @(posedge sample_clk or negedge sample_reset_n)
    if(!sample_reset_n)request_sync<=0;
    else request_sync<={request_sync[0],request_toggle};
  always @(posedge ctrl_clk or negedge ctrl_reset_n)
    if(!ctrl_reset_n)begin complete_sync<=0;error_sync<=0;end
    else begin
      complete_sync<={complete_sync[0],complete_toggle};
      error_sync<={error_sync[0],result_error};
    end

  always @(posedge ctrl_clk or negedge ctrl_reset_n)begin
    if(!ctrl_reset_n)begin
      request_toggle<=0;completion_pending<=0;
      busy<=0;done<=0;error<=0;rejected<=0;rd_valid<=0;
    end else begin
      rejected<=arm && busy;
      rd_valid<=rd_en && done && !error && !arm;
      if(arm && !busy)begin
        request_toggle<=~request_toggle;completion_pending<=0;
        busy<=1;done<=0;error<=0;
      end else if(busy && complete_sync[1]==request_toggle)begin
        // Keep the frozen payload for another destination clock after seeing
        // completion, rather than consuming independently synchronized bits
        // on the very first acknowledgement edge.
        if(!completion_pending)completion_pending<=1;
        else begin busy<=0;done<=1;error<=error_sync[1];end
      end
    end
  end
  // No reset on memory/read data: a valid read is the only ownership grant.
  // Before that rd_data is unspecified, and rd_valid is guaranteed low.
  always @(posedge ctrl_clk)
    if(ctrl_reset_n && rd_en && done && !error && !arm)rd_data<=memory[rd_addr];

  reg [1:0] state;
  reg [ADDR_BITS-1:0] write_address;
  reg [31:0] warmup_left;
  wire write_sample=state==CAPTURE && sample_ready && sample_valid && warmup_left==0;
  always @(posedge sample_clk)
    if(sample_reset_n && write_sample)
      memory[write_address]<={5'b0,otr2,data2,5'b0,otr1,data1};
  always @(posedge sample_clk or negedge sample_reset_n)begin
    if(!sample_reset_n)begin
      state<=WAIT_REQUEST;handled_request<=0;complete_toggle<=0;
      result_error<=0;write_address<=0;warmup_left<=0;
    end else case(state)
      WAIT_REQUEST:if(request_sync[1]!=handled_request)begin
        handled_request<=request_sync[1];result_error<=0;
        write_address<=0;warmup_left<=WARMUP_SAMPLES;state<=CAPTURE;
      end
      CAPTURE:begin
        if(!sample_ready)begin result_error<=1;state<=COMPLETE;end
        else if(sample_valid)begin
          if(warmup_left!=0)warmup_left<=warmup_left-1'b1;
          else if(&write_address)state<=COMPLETE;
          else write_address<=write_address+1'b1;
        end
      end
      // Completion changes at least one sample cycle after the final write or
      // error payload. The frozen RAM/error are held until a new arm request.
      COMPLETE:begin complete_toggle<=handled_request;state<=WAIT_REQUEST;end
      default:begin result_error<=1;state<=COMPLETE;end
    endcase
  end
endmodule
