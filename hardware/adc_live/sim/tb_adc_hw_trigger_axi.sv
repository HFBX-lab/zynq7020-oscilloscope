`timescale 1ns/1ps
module tb_adc_hw_trigger_axi;
  parameter integer ADDR_BITS=4;
  localparam integer DEPTH=1<<ADDR_BITS;
 `ifdef POST_SYNTH
  localparam integer TIMEOUT=1000000;
 `else
  localparam integer TIMEOUT=256;
 `endif
  reg clk=0,sample_clk=0,resetn=0;
  always #5 clk=~clk;
  always #100 sample_clk=~sample_clk;
  reg [19:0] external_index=0;
  always @(negedge sample_clk)external_index<=external_index+1'b1;
  wire [9:0] external_d1=external_index[9:0];
  wire [9:0] external_d2=(10'd1023-external_index[9:0])^external_index[19:10];
  reg input_ready=1;
  reg [15:0] awaddr=0,araddr=0;
  reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
  reg [31:0] wdata=0;
  integer poll_limit=TIMEOUT;
  reg [3:0] wstrb=0;
  wire awready,wready,bvalid,arready,rvalid;
  wire [1:0] bresp,rresp;
  wire [31:0] rdata;
`ifdef POST_SYNTH
  adc_live_axi dut(
`else
  adc_live_axi #(.ADDR_BITS(ADDR_BITS),.TIMEOUT_CYCLES(TIMEOUT)) dut(
`endif
    .s_axi_aclk(clk),.s_axi_aresetn(resetn),.sample_clk(sample_clk),
    .input_ready(input_ready),.input_valid(external_index[2:0]==0),.adc_data1(external_d1),.adc_data2(external_d2),.adc_otr1(external_index[2]),.adc_otr2(external_index[3]),
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
  function automatic [31:0] expected_word(input integer index_value);
    integer a,b;
    begin a=index_value&1023;b=(1023-a)^((index_value>>10)&1023);
      expected_word=((b|(((index_value>>3)&1)<<10))<<16) | a | (((index_value>>2)&1)<<10);end
  endfunction
  task automatic verify_record;
    integer n,first_index;reg [31:0] word_value;
    read_reg(16'h4000,0,word_value);
    first_index=word_value[9:0] | ((word_value[25:16]^(1023-word_value[9:0]))<<10);
    for(n=0;n<DEPTH;n=n+1)begin
      read_reg(16'h4000+n*4,0,word_value);
      if(word_value!==expected_word((first_index+n*8)&1048575))$fatal(1,"record index %0d wrong %h",n,word_value);
    end
    begin_read(16'h4000);word_value=rdata;
    write_reg(16'h0008,1,15,2,2); // ARM must not invalidate a held read.
    write_reg(16'h0008,2,15,0,2); // Neither may software reset.
    if(!rvalid || rdata!==word_value || rresp!==0)$fatal(1,"ownership changed under held response");
    end_read();
    // AR and a complete AW/W request arrive together: the already accepted
    // read owns the record and the new ARM is rejected without stale data.
    fork
      write_reg(16'h0008,1,15,2,2);
      begin_read(16'h4000);
    join
    if(!rvalid || rresp!==0 || rdata!==word_value)$fatal(1,"concurrent AR/ARM arbitration");
    end_read();
  endtask
  task automatic reset_bus;
    resetn=0;#33;arvalid=0;rready=0;awvalid=0;wvalid=0;bready=0;
    resetn=1;repeat(24)tick();
    if(rvalid || bvalid)$fatal(1,"reset leaked outstanding response");
  endtask
  reg [31:0] value,before_progress;integer n;
  initial begin
    #1;resetn=1;#2;resetn=0;#121;resetn=1;repeat(24)tick();
    read_reg(4,0,value);if(value!==32'h00050000)$fatal(1,"hardware ABI");
    write_reg(16'h0040,16,15,0,2);write_reg(16'h0044,261889,15,0,2);
    write_reg(16'h0048,0,15,0,2);write_reg(16'h0048,DEPTH-1,15,0,2);
    write_reg(16'h004c,0,15,0,2);write_reg(16'h004c,1024,15,0,2);
    write_reg(16'h0044,170,1,0,0);read_reg(16'h0044,0,value);if(value!==32'h0001f4aa)$fatal(1,"trigger WSTRB merge");
    write_reg(16'h0044,261888,15,0,0);write_reg(16'h0048,4,15,0,0);write_reg(16'h0040,13,15,0,0);
    write_reg(8,1,15,0,0);read_reg(16'h005c,0,before_progress);repeat(TIMEOUT*4)tick();
    read_reg(12,0,value);if(!value[0]||value[1]||value[2]||value[3])$fatal(1,"NORMAL wait failed aftertotaldeadline %h",value);
    read_reg(16'h0054,0,value);if(!value[8])$fatal(1,"hardware waiting status");
    read_reg(16'h005c,0,value);if(value<=before_progress)$fatal(1,"waiting progress stopped");
    write_reg(16'h0044,128000,15,0,2);write_reg(16'h0040,0,15,0,2);write_reg(8,4,15,0,0);
    // No valid samples is a fault, distinct from valid samples withoutedges.
    write_reg(32,0,15,0,0);wait_record(1);read_reg(12,0,value);if(!value[3])$fatal(1,"stalled sample stream didnot timeout");
    write_reg(8,2,15,0,0);repeat(30)tick();write_reg(32,1,15,0,0);
    write_reg(16'h0044,128000,15,0,0);write_reg(8,1,15,0,0);poll_limit=DEPTH*200+10000;wait_record(0);
    read_reg(16'h0054,0,value);if(value!==1)$fatal(1,"matched flags");
    read_reg(16'h0058,0,value);if(value!==4)$fatal(1,"trigger index");
    read_reg(16'h0068,0,value);if(value<DEPTH)$fatal(1,"incomplete ring");
    read_reg(16'h400c,0,value);if(value[9:0]>500)$fatal(1,"rising previous sample");
    read_reg(16'h4010,0,value);if(value[9:0]<=500)$fatal(1,"rising current sample");
    verify_record();
    write_reg(8,4,15,0,0);write_reg(16'h0040,9,15,0,0);write_reg(8,1,15,0,0);wait_record(0);
    read_reg(16'h0054,0,value);if(value!==1)$fatal(1,"falling flags");
    read_reg(16'h400c,0,value);if(value[9:0]<500)$fatal(1,"falling previous sample");
    read_reg(16'h4010,0,value);if(value[9:0]>=500)$fatal(1,"falling current sample");
    verify_record();
    $display("ADC_HW_AXI_PASS: depth=%0d configvalidation/WSTRB/held-read, indefinite NORMAL progress, stall watchdog, matched rise/fall ring metadata andfullrecordorder",DEPTH);$finish;
  end
  initial begin #100000000;$fatal(1,"hardware triggerAXI globaltimeout");end
endmodule
