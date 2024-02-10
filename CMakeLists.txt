cmake_minimum_required(VERSION 3.22)
set(CMAKE_CXX_STANDARD 17) 


project(elide C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/W4) # warning level 4
else()
    add_compile_options(-Wall -Wextra -Wpedantic 
      -Wno-unused-const-variable -Wno-sign-compare -Wno-unused-function) # additional warnings
endif()


set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -fdiagnostics-color=always -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color=always -O2")

# https://github.com/facebook/folly/blob/main/CMake/FindLibUring.cmake
# https://stackoverflow.com/a/29316084/5305365 
# find_package(PkgConfig REQUIRED)

# libelide
add_library(elidecore
  src/lean_lsp.cpp
  # src/lib/datastructures
  src/lib/datastructures/abuf.cpp
  src/lib/datastructures/process.cpp
  # src/lib/views
  src/lib/views/ctrlp.cpp
  # src/lib/
  src/lib/lib.cpp
  src/lib/uri_encode.cpp
  )

# find_package(ssh CONFIG)
find_package(json-c REQUIRED)
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

add_library(imgui
  src/third_party/imgui/imgui.cpp
  src/third_party/imgui/imgui_draw.cpp
  src/third_party/imgui/imgui_tables.cpp
  src/third_party/imgui/imgui_widgets.cpp
  src/third_party/imgui/imgui_demo.cpp # TODO: do I need this?
  src/third_party/imgui/backends/imgui_impl_sdl2.cpp
  src/third_party/imgui/backends/imgui_impl_opengl3.cpp)
target_include_directories(imgui PUBLIC src/third_party src/third_party/imgui/)
target_link_libraries(imgui PUBLIC OpenGL::GL)
# target_link_libraries(imgui PUBLIC SDL2::SDL2)
target_link_libraries(imgui PUBLIC $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main> $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>)

target_include_directories(elidecore PUBLIC json-c::json-c ssh)
target_include_directories(elidecore PUBLIC include/)
target_include_directories(elidecore PUBLIC src/third_party)
target_include_directories(elidecore PUBLIC src/third_party/imgui/)
target_include_directories(elidecore PUBLIC src/third_party/subprocess)
target_link_libraries(elidecore PUBLIC json-c::json-c imgui)
install(TARGETS elidecore DESTINATION lib)

# elide
add_executable(elide src/main.cpp)
target_link_libraries(elide PRIVATE elidecore)

if (WIN32)
    add_custom_command(
        TARGET elide POST_BUILD
        COMMAND "${CMAKE_COMMAND}" -E copy_if_different "$<TARGET_FILE:SDL2::SDL2>" "$<TARGET_FILE_DIR:elide>"
        VERBATIM
    )
endif()

install(TARGETS elide DESTINATION bin)
install(FILES  "$<TARGET_RUNTIME_DLLS:elide>" DESTINATION bin)
install(FILES src/abbreviations.json DESTINATION bin)
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

