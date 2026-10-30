`timescale 1ns/1ps
// Independent sequencer so production millisecond delays can be verified without
// paying for hundreds of millions of irrelevant UNISIM VCO transitions.
module lcd_powerup #(
    parameter LCD_RESET_CYCLES=512000,
    parameter BACKLIGHT_CYCLES=8388608
)(
    input wire pixel_clk,reset,frame_start,
    output reg lcd_reset_n,lcd_bl
);
    reg [23:0] startup=0;
    initial begin lcd_reset_n=0;lcd_bl=0;end
    always @(posedge pixel_clk or posedge reset)begin
        if(reset)begin startup<=0;lcd_reset_n<=0;lcd_bl<=0;end
        else begin
            if(startup<BACKLIGHT_CYCLES)startup<=startup+1'b1;
            if(startup>=LCD_RESET_CYCLES-1)lcd_reset_n<=1;
            if(startup>=BACKLIGHT_CYCLES-1 && frame_start)lcd_bl<=1;
        end
    end
endmodule
