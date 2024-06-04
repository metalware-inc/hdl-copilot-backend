#pragma once
#include <array>
#include <string_view>
#include <tuple>

static constexpr auto MAX_STATIC_COMPLETION_NUM = 35;
static constexpr std::array<std::tuple<std::string_view /*name*/,
                                std::string_view /*text*/,
                                std::string_view /*description*/>,
    MAX_STATIC_COMPLETION_NUM>
    static_completions = {
        // CONSTRUCTS
        std::make_tuple("module"sv,
            R"(module ${1:name}(
  ${2:input logic clk},
  ${3:input logic rst}
);
  ${0}
endmodule : ${1:name})"sv,
            " Define a module"),
        std::make_tuple("program"sv,
            R"(program ${1:name};
  ${0}
endprogram : ${1:name})"sv,
            " Define a program"),
        std::make_tuple("class"sv,
            R"(class ${1:name};
  ${0}
endclass : ${1:name})"sv,
            " Define a class"),
        std::make_tuple("final"sv,
            R"(final begin
  ${0}
end)"sv,
            " Insert a final block"),
        std::make_tuple("interface"sv,
            R"(interface ${1:name};
  ${0}
endinterface : ${1:name})"sv,
            " Define an interface"),
        std::make_tuple("package"sv,
            R"(package ${1:name};
  ${0}
endpackage : ${1:name})"sv,
            " Define a package"),
        std::make_tuple("function"sv,
            R"(function ${1:ret_type} ${2:name}(${3:input});
  ${0}
endfunction : ${2:name})"sv,
            " Define a function"),
        std::make_tuple("task"sv,
            R"(task ${1:name}(${2:input});
  ${0}
endtask : ${1:name})"sv,
            " Define a task"),
        std::make_tuple("property"sv,
            R"(property ${1:name} (${2:input});
  ${0}
endproperty : ${1:name})"sv,
            " Define a property"),
        std::make_tuple("sequence"sv,
            R"(sequence ${1:name} (${2:input1, input2});
  ${0:input1 or input2};
endsequence : ${1:name})"sv,
            " Define a sequence"),
        std::make_tuple(
            "assert"sv, R"(${1:label}: assert property (${0:expr});)"sv, " Insert an assertion"),
        std::make_tuple(
            "assume"sv, R"(${1:label}: assume property (${0:expr});)"sv, " Insert an assumption"),
        std::make_tuple("cover"sv,
            R"(${1:label}: cover property (${2:expr}) ${0:statement};)"sv,
            " Insert a cover"),
        std::make_tuple("restrict"sv,
            R"(${1:label}: restrict property (${0:expr});)"sv,
            " Insert a restriction"),
        std::make_tuple("parameter"sv,
            R"(parameter ${1:type} ${2:name} = ${0:value};)"sv,
            " Define a parameter"),
        std::make_tuple("localparam"sv,
            R"(localparam ${1:type} ${2:name} = ${0:value};)"sv,
            " Define a local parameter"),
        std::make_tuple("typedef enum"sv,
            R"(typedef enum ${1:name} {
  ${2:ENUM1},
  ${3:ENUM2}
} ${0:enum_type};)"sv,
            " Define an enum"),
        std::make_tuple("generate"sv,
            R"(generate
  ${0}
endgenerate)"sv,
            " Generate block"),
        std::make_tuple("generate"sv,
            R"(genvar ${1:i};
generate
  for (${1:i} = 0; ${1:i} < ${2:nloop}; ${1:i} = ${1:i} + 1) begin : ${3:block}
    ${0}
  end
endgenerate)"sv,
            " Generate block with for loop"),
        // PROCEDURAL BLOCKS
        std::make_tuple("always_comb"sv,
            R"(always_comb begin
  ${0}
end)"sv,
            " Insert always_comb block"),
        std::make_tuple("always_ff"sv,
            R"(always_ff @(${1:posedge clk}) begin
  ${0}
end)"sv,
            " Insert always_ff block"),
        std::make_tuple("always"sv,
            R"(always @(${1:posedge clk}) begin
  ${0}
end)"sv,
            " Insert always block"),
        std::make_tuple("initial"sv,
            R"(initial begin
  ${0}
end)"sv,
            " Insert initial block"),
        // SYSTEM TASKS
        std::make_tuple("$info"sv, R"(\$info("${0:message}");)"sv, " Insert info message"),
        std::make_tuple("$warning"sv, R"(\$warning("${0:message}");)"sv, " Insert warning message"),
        std::make_tuple("$error"sv, R"(\$error("${0:message}");)"sv, " Insert error message"),
        std::make_tuple("$fatal"sv, R"(\$fatal("${0:message}");)"sv, " Insert fatal message"),
        std::make_tuple("$display"sv, R"(\$display("${0:message}");)"sv, " Insert display message"),
        std::make_tuple("$write"sv, R"(\$write("${0:message}");)"sv, " Insert write message"),
        std::make_tuple("$strobe"sv, R"(\$strobe("${0:message}");)"sv, " Insert strobe message"),
        std::make_tuple("$monitor"sv, R"(\$monitor("${0:message}");)"sv, " Insert monitor message"),
};