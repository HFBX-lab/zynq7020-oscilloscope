`timescale 1ns/1ps
// I2C and the GT reset-address strap can ONLY pull low or release.
module touch_pads(input wire [3:0] gpio_o,gpio_t,output wire [3:0] raw_i,
    inout wire tp_sda,tp_scl,tp_int,output wire tp_reset_n);
    IOBUF sda_buf(.I(1'b0),.T(gpio_t[0]|gpio_o[0]),.O(raw_i[0]),.IO(tp_sda));
    IOBUF scl_buf(.I(1'b0),.T(gpio_t[1]|gpio_o[1]),.O(raw_i[1]),.IO(tp_scl));
    IOBUF int_buf(.I(1'b0),.T(gpio_t[2]|gpio_o[2]),.O(raw_i[2]),.IO(tp_int));
    // AXI GPIO powers up tri-stated. Hold touch reset until software owns it.
    assign tp_reset_n=~gpio_t[3]&gpio_o[3];
    assign raw_i[3]=tp_reset_n;
endmodule
