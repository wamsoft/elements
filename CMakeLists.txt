###############################################################################
#  Copyright (c) 2016-2023 Joel de Guzman
#  Copyright (c) 2020 Michał Urbański
#
#  Distributed under the MIT License (https://opensource.org/licenses/MIT)
###############################################################################
cmake_minimum_required(VERSION 3.15.0)

add_compile_options("$<$<AND:$<C_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:C>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/utf-8>")
add_compile_options("$<$<AND:$<CXX_COMPILER_ID:MSVC>,$<COMPILE_LANGUAGE:CXX>>:/Zc:__cplusplus>")

project(elements LANGUAGES C CXX)

set(CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${CMAKE_CURRENT_SOURCE_DIR}/cmake")

# Set the minimum required C++ standard to C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(ElementsConfigCommon)

option(ELEMENTS_BUILD_EXAMPLES "build Elements library examples" ON)
option(ELEMENTS_ENABLE_LTO "enable link time optimization for Elements targets" OFF)
set(ELEMENTS_HOST_UI_LIBRARY "sdl" CACHE STRING "Host UI library to use (sdl, win)")

add_subdirectory(lib)

if (ELEMENTS_BUILD_EXAMPLES)
   set(ELEMENTS_ROOT ${PROJECT_SOURCE_DIR})
   add_subdirectory(examples)
endif()
