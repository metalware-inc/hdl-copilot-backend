cmake_minimum_required(VERSION 3.22)

project(
    HDLCopilot
    DESCRIPTION "A SystemVerilog LSP."
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(SPDLOG_FMT_EXTERNAL ON)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)

add_compile_options(
    $<$<CONFIG:Debug>:-pg>
)
add_subdirectory(vendor)
add_subdirectory(src)
add_subdirectory(tests)
