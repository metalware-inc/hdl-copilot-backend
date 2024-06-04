`include "inexistent.sv"
`include "existent.svh"

module main_module();
    initial begin
        $display(`ABC); // from existent
        $display(`INEXISTENT); // from inexistent.svh
    end
endmodule
