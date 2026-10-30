`timescale 1ns/1ps
// 50MHz candidate: drive195deg, capture0; output data spans a clock period.
// RAW50MSa/s and CIC3/R8 6.25MSa/s are provided in parallel.
module adc_frontend #(
 parameter integer CONVERSION_HZ=50000000,
 parameter real DRIVE_PHASE_DEG=195.0,
 parameter integer BASE_DECIMATION=(CONVERSION_HZ==50000000?8:CONVERSION_HZ==25000000?4:16)
)(
 (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk_50 CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk_50, FREQ_HZ 50000000" *) input wire clk_50,
 (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 resetn RST", X_INTERFACE_PARAMETER="XIL_INTERFACENAME resetn, POLARITY ACTIVE_LOW" *) input wire resetn,
 input wire [9:0] ad_data_1,ad_data_2,input wire ad_otr_1,ad_otr_2,
 output wire ad_clk_1,ad_clk_2,ad_oe_1,ad_oe_2,
 (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 sample_clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME sample_clk, FREQ_HZ 50000000" *) output wire sample_clk,
 output wire sample_ready,sample_valid,output wire clock_locked,
 output wire [9:0] data1,data2,output wire otr1,otr2,
 output wire raw_valid,output wire [9:0] raw_data1,raw_data2,output wire raw_otr1,raw_otr2
);
 wire fb_raw,fb,raw,drive_raw,drive_clk;
 MMCME2_BASE #(.CLKIN1_PERIOD(20.0),.DIVCLK_DIVIDE(1),.CLKFBOUT_MULT_F(12.0),.CLKOUT0_DIVIDE_F(600000000.0/CONVERSION_HZ),.CLKOUT1_DIVIDE(600000000/CONVERSION_HZ),.CLKOUT1_PHASE(DRIVE_PHASE_DEG),.STARTUP_WAIT("FALSE"))
 adc_mmcm(.CLKIN1(clk_50),.CLKFBIN(fb),.RST(!resetn),.PWRDWN(1'b0),.CLKFBOUT(fb_raw),.CLKFBOUTB(),
 .CLKOUT0(raw),.CLKOUT0B(),.CLKOUT1(drive_raw),.CLKOUT1B(),.CLKOUT2(),.CLKOUT2B(),.CLKOUT3(),.CLKOUT3B(),
 .CLKOUT4(),.CLKOUT5(),.CLKOUT6(),.LOCKED(clock_locked));
 BUFG fb_buf(.I(fb_raw),.O(fb));BUFG sample_buf(.I(raw),.O(sample_clk));
 BUFG drive_buf(.I(drive_raw),.O(drive_clk));
 wire release_n=resetn&&clock_locked;
 (* ASYNC_REG="TRUE" *) reg [1:0] release_sync=0;
 always @(posedge sample_clk or negedge release_n)
  if(!release_n)release_sync<=0;else release_sync<={release_sync[0],1'b1};
 // Each clock domain releases its own asynchronous reset synchronously.
 (* ASYNC_REG="TRUE" *) reg [1:0] drive_release_sync=0;
 always @(posedge drive_clk or negedge release_n)
  if(!release_n)drive_release_sync<=0;else drive_release_sync<={drive_release_sync[0],1'b1};
 reg [5:0] warmup=0;
 always @(posedge sample_clk or negedge release_n)
  if(!release_n)begin warmup<=0;end
  else if(!release_sync[1])begin warmup<=0;end
  else if(warmup<32)begin warmup<=warmup+1'b1;end
 assign sample_ready=release_sync[1]&&warmup==32;

 assign ad_oe_1=~release_sync[1];assign ad_oe_2=~release_sync[1];
 ODDR #(.DDR_CLK_EDGE("SAME_EDGE"),.INIT(1'b0),.SRTYPE("ASYNC"))
 clk1(.Q(ad_clk_1),.C(drive_clk),.CE(1'b1),.D1(1'b1),.D2(1'b0),.R(!drive_release_sync[1]),.S(1'b0));
 ODDR #(.DDR_CLK_EDGE("SAME_EDGE"),.INIT(1'b0),.SRTYPE("ASYNC"))
 clk2(.Q(ad_clk_2),.C(drive_clk),.CE(1'b1),.D1(1'b1),.D2(1'b0),.R(!drive_release_sync[1]),.S(1'b0));
 // Data and OTR are captured together; never synchronize the bus bit by bit.
 // Keep a single physical input bank shared by RAW and filtered paths.
 (* IOB="TRUE", DONT_TOUCH="TRUE" *) reg [21:0] captured;
 always @(posedge sample_clk)captured<={ad_otr_2,ad_data_2,ad_otr_1,ad_data_1};
 assign raw_valid=sample_ready;
 assign {raw_otr2,raw_data2,raw_otr1,raw_data1}=captured;
 adc_cic_decimator #(.R(BASE_DECIMATION)) base_filter(sample_clk,release_sync[1],sample_ready,
   captured[9:0],captured[20:11],captured[10],captured[21],sample_valid,data1,data2,otr1,otr2);
endmodule
