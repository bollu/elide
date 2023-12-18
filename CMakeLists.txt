cmake_minimum_required(VERSION 3.20)
project(ward C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fdiagnostics-color=always -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -O2")

add_executable(edtr main.cpp)

# https://github.com/facebook/folly/blob/main/CMake/FindLibUring.cmake
# https://stackoverflow.com/a/29316084/5305365 
find_package(PkgConfig REQUIRED)
pkg_check_modules(URING REQUIRED IMPORTED_TARGET liburing)

find_package(json-c CONFIG)
target_include_directories(edtr PRIVATE json-c::json-c PkgConfig::URING)
target_link_libraries(edtr PRIVATE json-c::json-c PkgConfig::URING)
install(TARGETS edtr DESTINATION bin)
