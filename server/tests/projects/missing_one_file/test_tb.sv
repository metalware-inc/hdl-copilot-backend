`include "definitions.svh"

module test_tb;
initial begin
  $display("Hello, World! %d", `FOO);
  $finish;
end
endmodule
