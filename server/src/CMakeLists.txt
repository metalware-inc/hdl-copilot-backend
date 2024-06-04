project(hdl_copilot_server)

# Create a library with utils.cpp and possibly other source files
add_library(hdl_copilot_server_lib packethandler.cpp project.cpp utils.cpp license.cpp languageclient.cpp shared.cpp rootunit.cpp)
add_library(diff-match-patch-cpp-stl INTERFACE)
target_include_directories(hdl_copilot_server_lib PRIVATE ${diff-match-patch-cpp-stl_SOURCE_DIR})
target_compile_features(hdl_copilot_server_lib PUBLIC cxx_std_20)

target_link_libraries(hdl_copilot_server_lib PUBLIC
        fmt::fmt
        nlohmann_json::nlohmann_json
        spdlog::spdlog
        slang::slang)

# Check if the system is Linux
if (${CMAKE_SYSTEM_NAME} MATCHES "Linux")
    set(LINK_FLAGS "-static -static-libgcc -static-libstdc++")
    target_link_libraries(hdl_copilot_server_lib PUBLIC ${LINK_FLAGS})
endif ()

# Create the server executable and link it with the library.
add_executable(${PROJECT_NAME} main.cpp)
target_link_libraries(${PROJECT_NAME} PRIVATE hdl_copilot_server_lib)
target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_20)
