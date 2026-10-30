`timescale 1ns/1ps
// Shared RGB electrical layer for the PS/VDMA platform. Panel behavior is candidate.
module screen_rgb_io(
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk_50 CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk_50, FREQ_HZ 50000000" *)
    input wire clk_50,
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 pixel_clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME pixel_clk, FREQ_HZ 51200000" *)
    output wire pixel_clk,
    output wire pixel_locked,
    input wire [23:0] video_data,
    input wire video_de,video_hsync,video_vsync,video_locked,
    output wire [7:0] lcd_r,lcd_g,lcd_b,
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 lcd_clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME lcd_clk, FREQ_HZ 51200000" *)
    output wire lcd_clk,
    output reg lcd_de,lcd_hsync,lcd_vsync,
    output wire lcd_bl,lcd_reset_n,tp_reset_n
);
    wire fb_raw,fb,clk_raw;
    MMCME2_BASE #(.CLKIN1_PERIOD(20.0),.DIVCLK_DIVIDE(5),.CLKFBOUT_MULT_F(64.0),
        .CLKOUT0_DIVIDE_F(12.5),.STARTUP_WAIT("FALSE"))
    pixel_mmcm(.CLKIN1(clk_50),.CLKFBIN(fb),.RST(1'b0),.PWRDWN(1'b0),
        .CLKFBOUT(fb_raw),.CLKFBOUTB(),.CLKOUT0(clk_raw),.CLKOUT0B(),.CLKOUT1(),.CLKOUT1B(),
        .CLKOUT2(),.CLKOUT2B(),.CLKOUT3(),.CLKOUT3B(),.CLKOUT4(),.CLKOUT5(),.CLKOUT6(),.LOCKED(pixel_locked));
    BUFG fb_buffer(.I(fb_raw),.O(fb));
    BUFG pixel_buffer(.I(clk_raw),.O(pixel_clk));
    wire ready_async=pixel_locked && video_locked;
    (* ASYNC_REG="TRUE" *) reg [1:0] ready_sync=0;
    always @(posedge pixel_clk or negedge ready_async)
        if(!ready_async)ready_sync<=0;else ready_sync<={ready_sync[0],1'b1};
    wire reset=!ready_sync[1];
    reg previous_vsync=0;
    always @(posedge pixel_clk or posedge reset)
        if(reset)previous_vsync<=0;else previous_vsync<=video_vsync;
    wire frame_start=!previous_vsync && video_vsync;
    lcd_powerup powerup(.pixel_clk(pixel_clk),.reset(reset),.frame_start(frame_start),
        .lcd_reset_n(lcd_reset_n),.lcd_bl(lcd_bl));
    (* IOB="TRUE" *) reg [23:0] rgb_out=0;
    initial begin lcd_de=0;lcd_hsync=1;lcd_vsync=1;end
    always @(posedge pixel_clk or posedge reset)begin
        if(reset)begin rgb_out<=0;lcd_de<=0;lcd_hsync<=1;lcd_vsync<=1;end
        else begin
            rgb_out<=(video_de && lcd_reset_n)?video_data:24'd0;
            lcd_de<=video_de && lcd_reset_n;
            // PG044 requires positive internal sync; the candidate panel uses negative sync.
            lcd_hsync<=!video_hsync;lcd_vsync<=!video_vsync;
        end
    end
    assign {lcd_r,lcd_g,lcd_b}=rgb_out;
    assign tp_reset_n=1'b0;
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"),.INIT(1'b0),.SRTYPE("SYNC"))
        forward_clock(.Q(lcd_clk),.C(pixel_clk),.CE(1'b1),.D1(1'b0),.D2(1'b1),.R(1'b0),.S(1'b0));
endmodule
