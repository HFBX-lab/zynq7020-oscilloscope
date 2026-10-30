`timescale 1ns/1ps
module tb_adc_axi;
  parameter integer ADDR_BITS=4;
  localparam integer DEPTH=1<<ADDR_BITS;
  localparam integer TIMEOUT=DEPTH*4+256;
  reg clk=0,sample_clk=0,resetn=0;
  always #5 clk=~clk;
  always #10 sample_clk=~sample_clk;
  reg [15:0] awaddr=0,araddr=0;
  reg awvalid=0,wvalid=0,bready=0,arvalid=0,rready=0;
  reg [31:0] wdata=0;
  integer poll_limit=TIMEOUT;
  reg [3:0] wstrb=0;
  wire awready,wready,bvalid,arready,rvalid;
  wire [1:0] bresp,rresp;
  wire [31:0] rdata;
`ifdef POST_SYNTH
  adc_axi_capture dut(
`else
  adc_axi_capture #(.ADDR_BITS(ADDR_BITS),.TIMEOUT_CYCLES(TIMEOUT)) dut(
`endif
    .s_axi_aclk(clk),.s_axi_aresetn(resetn),.sample_clk(sample_clk),
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
      if(word_value!==expected_word((first_index+n)&1048575))$fatal(1,"record index %0d wrong %h",n,word_value);
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
  reg [31:0] value;
  integer n;
  initial begin
    #1;resetn=1;#2;resetn=0;#121;resetn=1;repeat(24)tick();
    read_reg(0,0,value);if(value!==32'h53414443)$fatal(1,"ID mismatch");
    read_reg(4,0,value);if(value!==32'h00010000)$fatal(1,"ABI mismatch");
    read_reg(16,0,value);if(value!==DEPTH)$fatal(1,"depth mismatch");
    read_reg(20,0,value);if(value!==DEPTH*4)$fatal(1,"bytes mismatch");
    read_reg(28,0,value);
    poll_limit=value;
`ifndef POST_SYNTH
    if(value!==TIMEOUT)$fatal(1,"timeout ABI mismatch");
`endif
    read_reg(16'h4000,2,value);read_reg(16'h4001,2,value);read_reg(16'h0024,2,value);
    write_reg(0,0,15,2,2);write_reg(9,1,15,0,2);write_reg(8,8,15,1,2);
    write_reg(8,1,2,2,0);read_reg(12,0,value);if(value[0] || value[1])$fatal(1,"WSTRB ignored");
    write_reg(32,0,2,1,0);read_reg(32,0,value);if(value!==1)$fatal(1,"TEST_CONTROL strobe ignored");
    write_reg(8,1,15,0,0);write_reg(8,1,15,1,2); // busy rejection
    read_reg(16'h4000,2,value);
    wait_record(0);verify_record();
    read_reg(24,0,value);if(value!==1)$fatal(1,"first capture ID");
    write_reg(8,4,15,1,0);read_reg(12,0,value);if(value[5])$fatal(1,"clear events");
    write_reg(8,1,15,2,0);wait_record(0);verify_record();
    read_reg(24,0,value);if(value!==2)$fatal(1,"second capture ID");
    write_reg(32,0,15,1,0);repeat(10)@(posedge sample_clk);
    // Abort an active acquisition before its deadline; reset must not create
    // a successful record or leave a completion toggle for the next ARM.
    write_reg(8,1,15,2,0);write_reg(8,2,15,2,0);repeat(30)tick();
    read_reg(12,0,value);if(value!==64)$fatal(1,"active software reset state");
    read_reg(24,0,value);if(value!==2)$fatal(1,"abort changed capture ID");
    write_reg(8,1,15,2,0);write_reg(8,1,15,2,2);wait_record(1);
    read_reg(12,0,value);if(!value[3] || !value[5])$fatal(1,"timeout must preserve sticky rejection %h",value);
    read_reg(16'h4000,2,value);
    write_reg(8,2,15,0,0);repeat(30)tick();
    read_reg(12,0,value);if((value&63)!=0)$fatal(1,"soft reset state %h",value);
    write_reg(32,1,15,1,0);write_reg(8,1,15,2,0);wait_record(0);verify_record();
    read_reg(24,0,value);if(value!==3)$fatal(1,"soft reset corrupted capture ID");
    // Cancel a BRAM read while pending, before its response is published.
    @(negedge clk);araddr=16'h4000;arvalid=1;
    do begin @(posedge clk);end while(!arready);
    #1;reset_bus();
    begin_read(0);reset_bus(); // held register RVALID
    @(negedge clk);awaddr=8;awvalid=1;
    do begin @(posedge clk);end while(!awready);
    #1;reset_bus(); // unmatched AW
    @(negedge clk);wdata=1;wstrb=15;wvalid=1;
    do begin @(posedge clk);end while(!wready);
    #1;reset_bus(); // unmatched W
    @(negedge clk);awaddr=8;awvalid=1;wdata=4;wstrb=15;wvalid=1;
    tick();@(negedge clk);awvalid=0;wvalid=0;
    n=0;while(!bvalid)begin tick();n=n+1;if(n>100)$fatal(1,"held B not returned");end
    reset_bus();
    read_reg(0,0,value);if(value!==32'h53414443)$fatal(1,"AXI reset recovery");
    write_reg(8,4,15,2,0); // write channels also recover after cancellation.
    $display("PASS: ADC AXI depth=%0d split-AW/W backpressure WSTRB errors ownership records timeout reset",DEPTH);$finish;
  end
  initial begin #100000000;$fatal(1,"test global timeout");end
endmodule
