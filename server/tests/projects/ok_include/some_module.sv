`include "some_defines.svh"

module some_module#(
    parameter int WIDTH = `WIDTH)
    (
    input logic clk, input logic [WIDTH-1:0] a, output logic [WIDTH-1:0] b
    );
    always_ff @(posedge clk) begin
        b <= a;
    end
endmodule
