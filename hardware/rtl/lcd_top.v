`timescale 1ns/1ps
// V1.3B, xc7z020clg484-1, J35, PL-only bring-up. No PS/DDR/ADC.
module lcd_top #(
    parameter PATTERN_MODE=0,
    parameter LCD_RESET_CYCLES=512000,
    parameter BACKLIGHT_CYCLES=8388608
)(
    input wire clk_50,
    output wire [7:0] lcd_r,lcd_g,lcd_b,
    output wire lcd_clk,
    output reg lcd_hsync,lcd_vsync,lcd_de,
    output wire lcd_bl,lcd_reset_n,
    output wire tp_reset_n
);
    wire feedback_raw,feedback,clock_raw,pixel_clk,locked;
    // 50 / 5 * 64 = 640 MHz VCO; 640 / 12.5 = 51.2 MHz.
    MMCME2_BASE #(.CLKIN1_PERIOD(20.0),.DIVCLK_DIVIDE(5),
        .CLKFBOUT_MULT_F(64.0),.CLKOUT0_DIVIDE_F(12.5),.STARTUP_WAIT("FALSE"))
    pixel_mmcm(.CLKIN1(clk_50),.CLKFBIN(feedback),.RST(1'b0),.PWRDWN(1'b0),
        .CLKFBOUT(feedback_raw),.CLKFBOUTB(),.CLKOUT0(clock_raw),.CLKOUT0B(),
        .CLKOUT1(),.CLKOUT1B(),.CLKOUT2(),.CLKOUT2B(),.CLKOUT3(),.CLKOUT3B(),
        .CLKOUT4(),.CLKOUT5(),.CLKOUT6(),.LOCKED(locked));
    BUFG fb_buffer(.I(feedback_raw),.O(feedback));
    BUFG pixel_buffer(.I(clock_raw),.O(pixel_clk));
    // Asynchronous assertion on lost lock, synchronous release after 2 cycles.
    (* ASYNC_REG="TRUE" *) reg [1:0] reset_sync=0;
    always @(posedge pixel_clk or negedge locked)
        if(!locked)reset_sync<=0;else reset_sync<={reset_sync[0],1'b1};
    wire reset=!reset_sync[1];
    wire frame_start;
    initial begin lcd_hsync=1;lcd_vsync=1;lcd_de=0;end
    lcd_powerup #(.LCD_RESET_CYCLES(LCD_RESET_CYCLES),.BACKLIGHT_CYCLES(BACKLIGHT_CYCLES))
        powerup(.pixel_clk(pixel_clk),.reset(reset),.frame_start(frame_start),
                .lcd_reset_n(lcd_reset_n),.lcd_bl(lcd_bl));
    // 10 ms reset hold; backlight opens at the first complete frame after >=164 ms.
    assign tp_reset_n=1'b0; // Touch intentionally held reset until an I2C driver exists.
    wire hs,vs,de;wire [23:0] rgb;wire [11:0] x,y;
    lcd_timing scan(.pixel_clk(pixel_clk),.reset(reset || !lcd_reset_n),
        .hsync(hs),.vsync(vs),.de(de),.pixel_x(x),.pixel_y(y),.frame_start(frame_start));
    lcd_test_pattern pattern(.pixel_clk(pixel_clk),.reset(reset || !lcd_reset_n),
        .mode(PATTERN_MODE[1:0]),.de(de),.pixel_x(x),.pixel_y(y),.frame_start(frame_start),.rgb(rgb));
    (* IOB="TRUE" *) reg [23:0] rgb_out=0;
    always @(posedge pixel_clk or posedge reset)begin
        if(reset)begin rgb_out<=0;lcd_hsync<=1;lcd_vsync<=1;lcd_de<=0;end
        else begin rgb_out<=rgb;lcd_hsync<=hs;lcd_vsync<=vs;lcd_de<=de;end
    end
    assign {lcd_r,lcd_g,lcd_b}=rgb_out;
    // Outputs launch on internal rising edge; panel rising edge is half a cycle later.
    // This edge choice is a candidate pending real panel/oscilloscope confirmation.
    ODDR #(.DDR_CLK_EDGE("SAME_EDGE"),.INIT(1'b0),.SRTYPE("SYNC"))
        forward_clock(.Q(lcd_clk),.C(pixel_clk),.CE(1'b1),.D1(1'b0),.D2(1'b1),.R(1'b0),.S(1'b0));
endmodule
