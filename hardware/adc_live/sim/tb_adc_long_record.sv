`timescale 1ns/1ps
// Functional acceleration only: clocks/valid are accelerated together. This
// checks all4096 addresses, constant gain, cadence ownership and slow config;
// real-rate timeouts/cadence are separately exercised by tb_adc_live_axi.
module tb_adc_long_record;
 localparam ADDR_BITS=12;
 reg clk=0,sample_clk=0,resetn=0;always #5 clk=~clk;always #7 sample_clk=~sample_clk;
 reg input_ready=1;wire [9:0] external_d1=521,external_d2=200;
  reg [15:0] awaddr=0,araddr=0;
  reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
  reg [31:0] wdata=0;
  integer poll_limit=10000000;
  reg [3:0] wstrb=0;
  wire awready,wready,bvalid,arready,rvalid;
  wire [1:0] bresp,rresp;
  wire [31:0] rdata;
`ifdef POST_SYNTH
  adc_live_axi dut(
`else
  adc_live_axi #(.ADDR_BITS(ADDR_BITS),.TIMEOUT_CYCLES(1000000)) dut(
`endif
    .s_axi_aclk(clk),.s_axi_aresetn(resetn),.sample_clk(sample_clk),
    .input_ready(input_ready),.input_valid(1'b1),.adc_data1(external_d1),.adc_data2(external_d2),.adc_otr1(1'b0),.adc_otr2(1'b0),
    .s_axi_awaddr(awaddr),.s_axi_awprot(3'b0),.s_axi_awvalid(awvalid),.s_axi_awready(awready),
    .s_axi_wdata(wdata),.s_axi_wstrb(wstrb),.s_axi_wvalid(wvalid),.s_axi_wready(wready),
    .s_axi_bresp(bresp),.s_axi_bvalid(bvalid),.s_axi_bready(bready),
    .s_axi_araddr(araddr),.s_axi_arprot(3'b0),.s_axi_arvalid(arvalid),.s_axi_arready(arready),
    .s_axi_rdata(rdata),.s_axi_rresp(rresp),.s_axi_rvalid(rvalid),.s_axi_rready(rready));
  task automatic tick;@(posedge clk);#1;endtask
  task automatic write_reg(input [15:0] address,input [31:0] value,input [3:0] strobes,
                           input integer order,input [1:0] response);
    integer n;
    fork
      begin
        if(order==1)repeat(5)@(negedge clk);
        @(negedge clk);awaddr=address;awvalid=1;n=0;
        do begin @(posedge clk);n=n+1;if(n>100)$fatal(1,"AW timeout");end while(!awready);
        @(negedge clk);awvalid=0;
      end
      begin
        if(order==0)repeat(5)@(negedge clk);
        @(negedge clk);wdata=value;wstrb=strobes;wvalid=1;
        begin: wait_w
          integer count_w;count_w=0;
          do begin @(posedge clk);count_w=count_w+1;if(count_w>100)$fatal(1,"W timeout");end while(!wready);
        end
        @(negedge clk);wvalid=0;
      end
    join
    n=0;while(!bvalid)begin tick();n=n+1;if(n>100)$fatal(1,"B timeout");end
    repeat(3)begin tick();if(!bvalid || bresp!==response)$fatal(1,"B response/hold mismatch addr=%h resp=%h",address,bresp);end
    @(negedge clk);bready=1;tick();@(negedge clk);bready=0;
  endtask
  task automatic begin_read(input [15:0] address);
    integer n;
    @(negedge clk);araddr=address;arvalid=1;n=0;
    do begin @(posedge clk);n=n+1;if(n>100)$fatal(1,"AR timeout");end while(!arready);
    @(negedge clk);arvalid=0;n=0;
    while(!rvalid)begin tick();n=n+1;if(n>100)$fatal(1,"R timeout");end
  endtask
  task automatic end_read;
    @(negedge clk);rready=1;tick();@(negedge clk);rready=0;
  endtask
  task automatic read_reg(input [15:0] address,input [1:0] response,output reg [31:0] value);
    begin_read(address);value=rdata;
    if(rresp!==response)$fatal(1,"R response mismatch addr=%h resp=%h",address,rresp);
    repeat(2)begin tick();if(!rvalid || rdata!==value || rresp!==response)$fatal(1,"R backpressure stability");end
    end_read();
  endtask
  task automatic wait_record(input integer expected_error);
    integer n;reg [31:0] status;
    n=0;status=0;
    while((status&2)==0 || (status&16)!=0)begin
      read_reg(16'h000c,0,status);n=n+1;if(n>poll_limit)$fatal(1,"poll timeout");
    end
    if(status[0] || status[2]!==expected_error[0] || status[7]===expected_error[0])
      $fatal(1,"record status invalid %h",status);
  endtask

 reg [31:0] value;integer n,tries;
 initial begin
  #1;resetn=1;#2;resetn=0;#121;resetn=1;repeat(24)tick();
  write_reg(16'h0030,3,15,0,0);tries=0;
  read_reg(16'h0034,0,value);
  while(value[0]||value[9:8]!=3)begin read_reg(16'h0034,0,value);tries=tries+1;if(tries>500)$fatal(1,"slow config timeout");end
  read_reg(16'h0024,0,value);if(value!==625)$fatal(1,"625rate metadata");
  write_reg(8,1,15,0,0);wait_record(0);
  for(n=0;n<4096;n=n+1)begin read_reg(16'h4000+4*n,0,value);if(value!==32'h00c80209)$fatal(1,"full slow word %0d=%h",n,value);end
  $display("ADC_LONG_RECORD_PASS: accelerated full4096 slow profile, independent DC gain, format, all addresses and completion");$finish;
 end
 initial begin #100000000;$fatal(1,"long record global timeout");end
endmodule
