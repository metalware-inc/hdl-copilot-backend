`include "some_module.sv"

module main;
  logic clk;
  logic [3:0] a;
  logic [3:0] b;

  some_module uut (
    .clk(clk),
    .a(a),
    .b(b)
  );

  initial begin
    clk = 0;
    a = 4'b1010;
    #5;
    $display("a = %b, b = %b", a, b);
    a = 4'b0101;
    #5;
    $display("a = %b, b = %b", a, b);
    $finish;
  end
endmodule
