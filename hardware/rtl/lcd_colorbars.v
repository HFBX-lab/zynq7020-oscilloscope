`timescale 1ns/1ps
// DE-mode candidate timing; actual module still requires testing.
module lcd_colorbars #(
    parameter H_ACTIVE=1024, H_SYNC=20, H_BACK=140, H_FRONT=160,
    parameter V_ACTIVE=600, V_SYNC=3, V_BACK=20, V_FRONT=12
)(
    input wire pixel_clk, input wire reset,
    output wire hsync, output wire vsync, output wire de,
    output reg [23:0] rgb
);
    wire [11:0] x,y;
    wire frame_start;
    lcd_timing #(.H_ACTIVE(H_ACTIVE),.H_SYNC(H_SYNC),.H_BACK(H_BACK),.H_FRONT(H_FRONT),
        .V_ACTIVE(V_ACTIVE),.V_SYNC(V_SYNC),.V_BACK(V_BACK),.V_FRONT(V_FRONT))
    scan(.pixel_clk(pixel_clk),.reset(reset),.hsync(hsync),.vsync(vsync),.de(de),
        .pixel_x(x),.pixel_y(y),.frame_start(frame_start));
    always @* begin
        rgb=24'h000000;
        if(de) begin
            if(x<H_ACTIVE/8) rgb=24'hffffff;
            else if(x<2*H_ACTIVE/8) rgb=24'hffff00;
            else if(x<3*H_ACTIVE/8) rgb=24'h00ffff;
            else if(x<4*H_ACTIVE/8) rgb=24'h00ff00;
            else if(x<5*H_ACTIVE/8) rgb=24'hff00ff;
            else if(x<6*H_ACTIVE/8) rgb=24'hff0000;
            else if(x<7*H_ACTIVE/8) rgb=24'h0000ff;
        end
    end
endmodule
