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

# elements_modal: JSON modal UI library (SDL host only)
if (ELEMENTS_HOST_UI_LIBRARY STREQUAL "sdl")
   option(ELEMENTS_BUILD_MODAL "Build elements_modal JSON modal UI library" ON)
   if (ELEMENTS_BUILD_MODAL)
      set(ELEMENTS_MODAL_BUILD_EXAMPLES ${ELEMENTS_BUILD_EXAMPLES} CACHE BOOL "" FORCE)
      add_subdirectory(external/elements_modal)

      # elements_modal 本体はフォルダ管理を持たない方針 (吉里吉里組み込み時は
      # 本体側が Storages 経由で font を登録する)。 standalone デモは
      # elements リポの resources/fonts を絶対パスで指す compile def で
      # 自前ロードする。
      if (TARGET elements_modal_hello_example)
         target_compile_definitions(elements_modal_hello_example PRIVATE
            ELEMENTS_MODAL_DEMO_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts"
         )
      endif()
   endif()
endif()

if (ELEMENTS_BUILD_EXAMPLES)
   set(ELEMENTS_ROOT ${PROJECT_SOURCE_DIR})
   add_subdirectory(examples)
endif()
