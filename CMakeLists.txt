cmake_minimum_required(VERSION 3.20)

project(ward C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fdiagnostics-color=always -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -O2")

# https://github.com/facebook/folly/blob/main/CMake/FindLibUring.cmake
# https://stackoverflow.com/a/29316084/5305365 
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)
pkg_check_modules(UV REQUIRED IMPORTED_TARGET libuv)

# libedtr
add_library(edtr src/lib/lib.cpp src/lean_lsp.cpp src/lib/uri_encode.cpp)
find_package(json-c CONFIG)
target_include_directories(edtr PUBLIC json-c::json-c PkgConfig::URING PkgConfig::UV)
target_include_directories(edtr PUBLIC include/)
target_link_libraries(edtr PUBLIC json-c::json-c PkgConfig::URING PkgConfig::UV)
install(TARGETS edtr DESTINATION lib)

# edtr
add_executable(elide src/main.cpp)
target_link_libraries(elide PRIVATE edtr)
install(TARGETS elide DESTINATION bin)
file(COPY src/abbreviations.json DESTINATION ${CMAKE_CURRENT_BINARY_DIR})

add_subdirectory(test/)


## ASAN
get_property(isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(isMultiConfig)
    if(NOT "Asan" IN_LIST CMAKE_CONFIGURATION_TYPES)
        list(APPEND CMAKE_CONFIGURATION_TYPES Asan)
    endif()
else()
    set(allowedBuildTypes Asan Debug Release RelWithDebInfo MinSizeRel)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "${allowedBuildTypes}")

    if(CMAKE_BUILD_TYPE AND NOT CMAKE_BUILD_TYPE IN_LIST allowedBuildTypes)
        message(FATAL_ERROR "Invalid build type: ${CMAKE_BUILD_TYPE}")
    endif()
endif()

set(CMAKE_C_FLAGS_ASAN
    "${CMAKE_C_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
    "Flags used by the C compiler for Asan build type or configuration." FORCE)

set(CMAKE_CXX_FLAGS_ASAN
    "${CMAKE_CXX_FLAGS_DEBUG} -fsanitize=address -fno-omit-frame-pointer" CACHE STRING
    "Flags used by the C++ compiler for Asan build type or configuration." FORCE)

set(CMAKE_EXE_LINKER_FLAGS_ASAN
    "${CMAKE_EXE_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
    "Linker flags to be used to create executables for Asan build type." FORCE)

set(CMAKE_SHARED_LINKER_FLAGS_ASAN
    "${CMAKE_SHARED_LINKER_FLAGS_DEBUG} -fsanitize=address" CACHE STRING
    "Linker lags to be used to create shared libraries for Asan build type." FORCE)

## ASAN


