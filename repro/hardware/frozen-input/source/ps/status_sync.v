`timescale 1ns/1ps
// Independent diagnostic bits only; this is not a coherent multi-bit data bus.
module status_sync(
    (* X_INTERFACE_INFO="xilinx.com:signal:clock:1.0 clk CLK", X_INTERFACE_PARAMETER="XIL_INTERFACENAME clk, ASSOCIATED_RESET resetn" *) input wire clk,
    (* X_INTERFACE_INFO="xilinx.com:signal:reset:1.0 resetn RST", X_INTERFACE_PARAMETER="XIL_INTERFACENAME resetn, POLARITY ACTIVE_LOW" *) input wire resetn,
    input wire [3:0] async_status,
    output wire [3:0] status
);
    (* ASYNC_REG="TRUE" *) reg [3:0] stage1=0,stage2=0;
    always @(posedge clk)begin
        if(!resetn)begin stage1<=0;stage2<=0;end
        else begin stage1<=async_status;stage2<=stage1;end
    end
    assign status=stage2;
endmodule
