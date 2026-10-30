`timescale 1ns/1ps
// Bring-up graphics only: no measurement data, LVGL, PS, DDR, or ADC.
module lcd_test_pattern(
    input wire pixel_clk,reset,
    input wire [1:0] mode,
    input wire de,frame_start,
    input wire [11:0] pixel_x,pixel_y,
    output reg [23:0] rgb
);
    reg [7:0] phase=0;
    always @(posedge pixel_clk)begin
        if(reset)phase<=0;
        else if(frame_start)phase<=phase+8'd2;
    end
    wire [7:0] sine_address=pixel_x[7:0]+phase;
    wire signed [8:0] sine_value;
    wire signed [9:0] wave_y=10'sd300-sine_value;
    lcd_sine_rom sine(.address(sine_address),.value(sine_value));
    wire plot=pixel_x>=32 && pixel_x<=991 && pixel_y>=60 && pixel_y<=539;
    wire border=plot && (pixel_x==32 || pixel_x==991 || pixel_y==60 || pixel_y==539);
    // Constant comparisons avoid a long modulo/divider path on each pixel.
    wire [12:0] grid_columns;
    wire [8:0] grid_rows;
    genvar g;
    generate
        for(g=0;g<13;g=g+1)begin:columns
            assign grid_columns[g]=(pixel_x==32+g*80);
        end
        for(g=0;g<9;g=g+1)begin:rows
            assign grid_rows[g]=(pixel_y==60+g*60);
        end
    endgenerate
    wire grid=plot && ((|grid_columns) || (|grid_rows));
    wire axes=plot && (pixel_x==512 || pixel_y==300);
    wire trace=plot && pixel_y+12'd2>=wave_y && pixel_y<=wave_y+12'd2;
    wire [7:0] gray_terms[0:31];
    generate
        for(g=0;g<32;g=g+1)begin:gray_steps
            localparam [7:0] LEVEL=g*8;
            assign gray_terms[g]=(pixel_x>=32+g*30 && pixel_x<62+g*30)?LEVEL:8'd0;
        end
    endgenerate
    reg [7:0] gray_level;
    integer i;
    always @*begin
        gray_level=0;
        for(i=0;i<32;i=i+1)gray_level=gray_level | gray_terms[i];
    end
    always @* begin
        rgb=0;
        if(de && !reset)begin
            if(mode==0)begin
                case(pixel_x[9:7])
                    0:rgb=24'hffffff;1:rgb=24'hffff00;2:rgb=24'h00ffff;3:rgb=24'h00ff00;
                    4:rgb=24'hff00ff;5:rgb=24'hff0000;6:rgb=24'h0000ff;7:rgb=0;
                endcase
            end else begin
                rgb=24'h101925;
                if(grid)rgb=24'h29384b;
                if(axes)rgb=24'h526981;
                if(border)rgb=24'h8798af;
                if(mode==2 && trace)rgb=24'h40e0c0;
                // Fixed RGB primaries/complements at the top reveal bit order and wrong colors.
                if(pixel_y>=16 && pixel_y<36 && pixel_x>=32 && pixel_x<992)begin
                    if(pixel_x<152)rgb=24'hffffff;
                    else if(pixel_x<272)rgb=24'hffff00;
                    else if(pixel_x<392)rgb=24'h00ffff;
                    else if(pixel_x<512)rgb=24'h00ff00;
                    else if(pixel_x<632)rgb=24'hff00ff;
                    else if(pixel_x<752)rgb=24'hff0000;
                    else if(pixel_x<872)rgb=24'h0000ff;
                    else rgb=0;
                end
                // 32-step gray ramp checks fine color levels independently from the trace.
                if(pixel_y>=562 && pixel_y<584 && pixel_x>=32 && pixel_x<992)
                    rgb={gray_level,gray_level,gray_level};
            end
        end
    end
endmodule
