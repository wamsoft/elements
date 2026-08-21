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

# elements_modal: JSON modal UI library。
# ライブラリ本体の overlay サブセット (overlay_session / navigator) は host 非依存
# で、 win32 host でも SDL3 を引かずにビルドできる。 独立ウィンドウ modal
# (run_modal) だけが SDL host 専用 (elements_modal 側 CMake で modal.cpp を
# ELEMENTS_HOST_UI_LIBRARY で分岐)。
option(ELEMENTS_BUILD_MODAL "Build elements_modal JSON modal UI library" ON)
if (ELEMENTS_BUILD_MODAL)
   # standalone examples は run_modal を使う hello_modal を含むため SDL host のみ。
   if (ELEMENTS_HOST_UI_LIBRARY STREQUAL "sdl")
      set(ELEMENTS_MODAL_BUILD_EXAMPLES ${ELEMENTS_BUILD_EXAMPLES} CACHE BOOL "" FORCE)
   else()
      set(ELEMENTS_MODAL_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
   endif()
   # クロスホストサンプル (overlay_session のみ使用) は両 host でビルドする。
   set(ELEMENTS_MODAL_BUILD_CROSSHOST_EXAMPLE ${ELEMENTS_BUILD_EXAMPLES} CACHE BOOL "" FORCE)
   add_subdirectory(external/elements_modal)

   # elements_modal 本体はフォルダ管理を持たない方針 (吉里吉里組み込み時は
   # 本体側が Storages 経由で font を登録する)。 standalone デモは
   # elements リポの resources/fonts を絶対パスで指す compile def で
   # 自前ロードする。
   foreach (_em_demo elements_modal_hello_example elements_modal_navigator_example
                     elements_modal_crosshost_example elements_modal_partial_diff
                     elements_modal_layout_shot)
      if (TARGET ${_em_demo})
         target_compile_definitions(${_em_demo} PRIVATE
            ELEMENTS_MODAL_DEMO_FONTS_DIR="${PROJECT_SOURCE_DIR}/resources/fonts"
         )
      endif()
   endforeach()
endif()

if (ELEMENTS_BUILD_EXAMPLES)
   set(ELEMENTS_ROOT ${PROJECT_SOURCE_DIR})
   add_subdirectory(examples)
endif()
