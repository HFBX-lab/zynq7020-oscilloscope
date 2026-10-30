`timescale 1ns/1ps
// Pixel scan contract: valid coordinates are zero-based; blanking coordinates are zero.
module lcd_timing #(
    parameter H_ACTIVE=1024,H_SYNC=20,H_BACK=140,H_FRONT=160,
    parameter V_ACTIVE=600,V_SYNC=3,V_BACK=20,V_FRONT=12
)(
    input wire pixel_clk,input wire reset,
    output wire hsync,vsync,de,
    output wire [11:0] pixel_x,pixel_y,
    output wire frame_start
);
    localparam H_TOTAL=H_SYNC+H_BACK+H_ACTIVE+H_FRONT;
    localparam V_TOTAL=V_SYNC+V_BACK+V_ACTIVE+V_FRONT;
    reg [11:0] h=0,v=0;
    always @(posedge pixel_clk)begin
        if(reset)begin h<=0;v<=0;end
        else if(h==H_TOTAL-1)begin
            h<=0;
            if(v==V_TOTAL-1)v<=0;else v<=v+1'b1;
        end else h<=h+1'b1;
    end
    assign hsync=reset ? 1'b1 : h>=H_SYNC;
    assign vsync=reset ? 1'b1 : v>=V_SYNC;
    assign de=!reset && h>=H_SYNC+H_BACK && h<H_SYNC+H_BACK+H_ACTIVE
                     && v>=V_SYNC+V_BACK && v<V_SYNC+V_BACK+V_ACTIVE;
    assign pixel_x=de ? h-(H_SYNC+H_BACK) : 12'd0;
    assign pixel_y=de ? v-(V_SYNC+V_BACK) : 12'd0;
    assign frame_start=!reset && h==0 && v==0;
endmodule
