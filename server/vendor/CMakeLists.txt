include(FetchContent)
include(ExternalProject)

function(get_metalware_repo_url var_url repo_name)
    if(DEFINED ENV{REPO_ACCESS_TOKEN})
        set(REPO_ACCESS_TOKEN $ENV{REPO_ACCESS_TOKEN})
        set(${var_url} "https://${REPO_ACCESS_TOKEN}@github.com/metalware-inc/${repo_name}.git" PARENT_SCOPE)
        message(STATUS "Using access token for ${repo_name}")
    else()
        set(${var_url} "git@github.com:metalware-inc/${repo_name}.git" PARENT_SCOPE)
        message(STATUS "Using ssh for ${repo_name}")
    endif()
endfunction()

FetchContent_Declare(
  fmt
  GIT_REPOSITORY "https://github.com/fmtlib/fmt.git"
  GIT_TAG "10.2.1"
)
add_subdirectory(fmt)

FetchContent_Declare(
  spdlog
  GIT_REPOSITORY "https://github.com/gabime/spdlog.git"
  GIT_TAG "v1.13.0"
)
add_subdirectory(spdlog)

FetchContent_Declare(
  catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG "v3.5.4"
)
add_subdirectory(catch2)

get_metalware_repo_url(SLANG_URL "slang")
FetchContent_Declare(
  slang
  #URL "/Users/drw/slang"
  GIT_REPOSITORY "${SLANG_URL}"
  GIT_SHALLOW ON
  GIT_TAG "master"
)
add_subdirectory(slang)

FetchContent_Declare(json URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz)
add_subdirectory(json)

if(WIN32)
	add_compile_definitions(USE_WINDOWS_SSPI) # needed to get SSL/TLS working for curl
	add_compile_definitions(USE_SCHANNEL)
endif()
