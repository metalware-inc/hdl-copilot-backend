# server/tests/CMakeLists.txt
project(hdl_copilot_server_tests)

# Link the library from src
add_executable(${PROJECT_NAME} test_utils.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE hdl_copilot_server_lib)
target_link_libraries(${PROJECT_NAME} PRIVATE Catch2::Catch2WithMain)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/src)
