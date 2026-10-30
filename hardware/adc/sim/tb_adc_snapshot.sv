`timescale 1ns/1ps
module tb_adc_snapshot;
  parameter integer ADDR_BITS=12;
  parameter integer WARMUP=4;
  localparam integer DEPTH=1<<ADDR_BITS;
  reg ctrl_clk=0,sample_clk=0,reset_n=0;
  always #5.5 ctrl_clk=~ctrl_clk;
  always #10 sample_clk=~sample_clk;
  reg arm=0,rd_en=0,sample_ready=1,allow_samples=1;
  reg [ADDR_BITS-1:0] rd_addr=0;
  wire busy,done,error,rejected,rd_valid;
  wire [31:0] rd_data;
  reg sample_valid=0;
  reg [9:0] data1=0,data2=0;
  reg otr1=0,otr2=0;
  integer cycle=0,sample_number=0;
  // ADC words are generated independently of the capture implementation.
  // A sequence advances only for valid samples, so gaps must not drop a word.
  always @(negedge sample_clk) begin
    cycle=cycle+1;
    sample_valid=allow_samples && (cycle%5!=0);
    data1=sample_number%1024;
    // Encode higher index bits too: every position of a 4096-word record
    // must be distinguishable, including a read-address alias above bit 9.
    data2=(1023-(sample_number%1024)) ^ ((sample_number>>10)&1023);
    otr1=(sample_number>>2)&1;otr2=(sample_number>>3)&1;
    if(sample_valid)sample_number=sample_number+1;
  end
`ifdef POST_SYNTH
  adc_snapshot_core dut(
`else
  adc_snapshot_core #(.ADDR_BITS(ADDR_BITS),.WARMUP_SAMPLES(WARMUP)) dut(
`endif
    .ctrl_clk(ctrl_clk),.sample_clk(sample_clk),.reset_n(reset_n),
    .arm(arm),.busy(busy),.done(done),.error(error),.rejected(rejected),
    .rd_en(rd_en),.rd_addr(rd_addr),.rd_valid(rd_valid),.rd_data(rd_data),
    .sample_ready(sample_ready),.sample_valid(sample_valid),
    .data1(data1),.data2(data2),.otr1(otr1),.otr2(otr2));
  task automatic tick;
    @(posedge ctrl_clk);#1;
  endtask
  task automatic request;
    @(negedge ctrl_clk);arm=1;tick();
    if(!busy || done || error)$fatal(1,"arm must acquire ownership and clear status");
    @(negedge ctrl_clk);arm=0;
  endtask
  task automatic finish_record;
    integer timeout;
    timeout=0;
    while(!done && timeout<DEPTH*8+100)begin tick();timeout=timeout+1;end
    if(!done || busy || error)$fatal(1,"capture failed or timed out");
  endtask
  task automatic read_word(input integer address,output reg [31:0] value);
    @(negedge ctrl_clk);rd_addr=address;rd_en=1;tick();
    if(!rd_valid)$fatal(1,"frozen record must be readable");
    value=rd_data;
    @(negedge ctrl_clk);rd_en=0;tick();
    if(rd_valid)$fatal(1,"rd_valid must track requests, not stick high");
  endtask
  task automatic verify_record;
    integer i,start_index,index_value,code,second_code;
    reg [31:0] value,expected;
    read_word(0,value);
    start_index=value[9:0] | ((value[25:16] ^ (1023-value[9:0]))<<10);
    for(i=0;i<DEPTH;i=i+1)begin
      read_word(i,value);index_value=start_index+i;code=index_value%1024;
      second_code=(1023-code) ^ ((index_value>>10)&1023);
      expected=((second_code|(((index_value>>3)&1)<<10))<<16) | code | (((index_value>>2)&1)<<10);
      if(value!==expected)$fatal(1,"word %0d got %08x expected %08x",i,value,expected);
    end
    repeat(20)tick();read_word(0,value);
    if((value[9:0] | ((value[25:16] ^ (1023-value[9:0]))<<10))!==start_index)$fatal(1,"completed record overwritten");
  endtask
  integer i,first_expected;
  reg [31:0] value;
  initial begin
    // Assert a real reset edge, then allow both local reset synchronizers out.
    #1;reset_n=1;#2;reset_n=0;#41;reset_n=1;repeat(8)tick();
    rd_en=1;tick();if(rd_valid || busy || done)$fatal(1,"reset/read-before-arm status");rd_en=0;
    allow_samples=0;request();repeat(25)tick();
    if(!busy || done)$fatal(1,"invalid samples must not fabricate a completed record");
    @(negedge ctrl_clk);arm=1;rd_en=1;tick();
    if(!rejected || !busy || rd_valid)$fatal(1,"busy arm/read must be rejected");
    @(negedge ctrl_clk);arm=0;rd_en=0;
    first_expected=sample_number+WARMUP;allow_samples=1;
    finish_record();read_word(0,value);
    if((value[9:0] | ((value[25:16] ^ (1023-value[9:0]))<<10))!==first_expected)
      $fatal(1,"warmup count incorrect: got %08x expected index %0d",value,first_expected);
    verify_record();
    // Simultaneous arm/read revokes old record, never returns a stale valid word.
    @(negedge ctrl_clk);arm=1;rd_en=1;tick();
    if(rd_valid || !busy || done)$fatal(1,"rearm must revoke old record atomically");
    @(negedge ctrl_clk);arm=0;rd_en=0;
    finish_record();verify_record();
    // A ready failure is observable, and partial data must remain unreadable.
    allow_samples=0;request();repeat(10)tick();sample_ready=0;
    i=0;while(!done && i<100)begin tick();i=i+1;end
    if(!done || !error || busy)$fatal(1,"sample-ready loss must return an error");
    rd_en=1;tick();if(rd_valid)$fatal(1,"partial aborted record exposed");rd_en=0;
    sample_ready=1;allow_samples=1;request();finish_record();verify_record();
    if(DEPTH>32)begin
      // Fail/reset after genuine writes, not just while waiting for valid data.
      request();repeat(WARMUP+20)@(posedge sample_clk);#1;
      if(!busy)$fatal(1,"partial-record test unexpectedly finished");
      sample_ready=0;i=0;
      while(!done && i<100)begin tick();i=i+1;end
      if(!done || !error || busy)$fatal(1,"partial capture abort failed");
      rd_en=1;tick();if(rd_valid)$fatal(1,"partial aborted RAM exposed");rd_en=0;
      sample_ready=1;request();finish_record();verify_record();
      request();repeat(WARMUP+20)@(posedge sample_clk);#1;
      reset_n=0;#43;reset_n=1;repeat(8)tick();
      if(busy || done || error)$fatal(1,"partial-write reset failed");
      request();finish_record();verify_record();
    end
    // Global reset during acquisition must clear both toggles and ownership.
    allow_samples=0;request();repeat(10)tick();reset_n=0;#43;reset_n=1;
    repeat(8)tick();if(busy || done || error)$fatal(1,"mid-capture reset did not clear state");
    allow_samples=1;request();finish_record();verify_record();
    $display("PASS: ADC snapshot depth=%0d warmup=%0d sequence/OTR/gaps/freeze/rearm/reject/abort/reset",DEPTH,WARMUP);
    $finish;
  end
  initial begin #10000000;$fatal(1,"global test timeout");end
endmodule
