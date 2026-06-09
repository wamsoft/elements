###############################################################################
#  Copyright (c) 2016-2024 Joel de Guzman
#  Copyright (c) 2020 Michał Urbański
#
#  Distributed under the MIT License (https://opensource.org/licenses/MIT)
###############################################################################
cmake_minimum_required(VERSION 3.16.0)

include(FetchContent)

###############################################################################
# Sources (and Resources)

set(ELEMENTS_SOURCES
   src/element/button.cpp
   src/element/child_window.cpp
   src/element/composite.cpp
   src/element/dial.cpp
   src/element/drag_and_drop.cpp
   src/element/element.cpp
   src/element/floating.cpp
   src/element/flow.cpp
   src/element/grid.cpp
   src/element/image.cpp
   src/element/label.cpp
   src/element/layer.cpp
   src/element/list.cpp
   src/element/menu.cpp
   src/element/popup.cpp
   src/element/port.cpp
   src/element/proxy.cpp
   src/element/range_slider.cpp
   src/element/selection.cpp
   src/element/slider.cpp
   src/element/status_bar.cpp
   src/element/style/border.cpp
   src/element/style/button.cpp
   src/element/style/check_box.cpp
   src/element/style/grid_lines.cpp
   src/element/style/icon_button.cpp
   src/element/style/menu.cpp
   src/element/style/misc.cpp
   src/element/style/radio_button.cpp
   src/element/style/slide_switch.cpp
   src/element/style/slider.cpp
   src/element/style/thumbwheel.cpp
   src/element/text.cpp
   src/element/thumbwheel.cpp
   src/element/tile.cpp
   src/element/tooltip.cpp
   src/support/canvas.cpp
   src/support/text_backend_tvg.cpp
   src/support/glyph_layout_ft.cpp
   src/support/draw_utils.cpp
   src/support/font.cpp
   src/support/glyphs.cpp
   src/support/pixmap.cpp
   src/support/receiver.cpp
   src/support/rect.cpp
   src/support/text_utils.cpp
   src/support/resource_paths.cpp
   src/support/theme.cpp
   src/support/payload.cpp
   src/view.cpp
)

set(ELEMENTS_HEADERS
   include/elements.hpp
   include/elements/app.hpp
   include/elements/base_view.hpp
   include/elements/element.hpp
   include/elements/element/align.hpp
   include/elements/element/button.hpp
   include/elements/element/collapsable.hpp
   include/elements/element/composite.hpp
   include/elements/element/dial.hpp
   include/elements/element/drag_and_drop.hpp
   include/elements/element/element.hpp
   include/elements/element/events_intercept.hpp
   include/elements/element/floating.hpp
   include/elements/element/flow.hpp
   include/elements/element/grid.hpp
   include/elements/element/image.hpp
   include/elements/element/indirect.hpp
   include/elements/element/hidable.hpp
   include/elements/element/label.hpp
   include/elements/element/layer.hpp
   include/elements/element/list.hpp
   include/elements/element/margin.hpp
   include/elements/element/menu.hpp
   include/elements/element/modal.hpp
   include/elements/element/popup.hpp
   include/elements/element/port.hpp
   include/elements/element/proxy.hpp
   include/elements/element/range_slider.hpp
   include/elements/element/selection.hpp
   include/elements/element/size.hpp
   include/elements/element/slider.hpp
   include/elements/element/status_bar.hpp
   include/elements/element/style.hpp
   include/elements/element/style/border.hpp
   include/elements/element/style/button.hpp
   include/elements/element/style/caption.hpp
   include/elements/element/style/check_box.hpp
   include/elements/element/style/dialog.hpp
   include/elements/element/style/grid_lines.hpp
   include/elements/element/style/group.hpp
   include/elements/element/style/icon_button.hpp
   include/elements/element/style/menu.hpp
   include/elements/element/style/message_box.hpp
   include/elements/element/style/misc.hpp
   include/elements/element/style/notebook.hpp
   include/elements/element/style/pane.hpp
   include/elements/element/style/radio_button.hpp
   include/elements/element/style/tab.hpp
   include/elements/element/style/text_entry.hpp
   include/elements/element/style/thumbwheel.hpp
   include/elements/element/text.hpp
   include/elements/element/thumbwheel.hpp
   include/elements/element/tile.hpp
   include/elements/element/tracker.hpp
   include/elements/support.hpp
   include/elements/support/canvas.hpp
   include/elements/support/circle.hpp
   include/elements/support/color.hpp
   include/elements/support/context.hpp
   include/elements/support/detail/canvas_impl.hpp
   include/elements/support/detail/scratch_context.hpp
   include/elements/support/detail/stb_image.h
   include/elements/support/draw_utils.hpp
   include/elements/support/font.hpp
   include/elements/support/glyphs.hpp
   include/elements/support/icon_ids.hpp
   include/elements/support/pixmap.hpp
   include/elements/support/point.hpp
   include/elements/support/receiver.hpp
   include/elements/support/rect.hpp
   include/elements/support/resource_paths.hpp
   include/elements/support/text_utils.hpp
   include/elements/support/theme.hpp
   include/elements/view.hpp
   include/elements/window.hpp
)

# Host UI library selection: "win32" (default on Windows) or "sdl"
set(ELEMENTS_HOST_UI_LIBRARY "sdl" CACHE STRING "Host UI library (win32 or sdl)")

if (ELEMENTS_HOST_UI_LIBRARY STREQUAL "sdl")
   set(ELEMENTS_HOST
      host/sdl/app.cpp
      host/sdl/base_view.cpp
      host/sdl/window.cpp
   )
else()
   set(ELEMENTS_HOST
      host/windows/utils.hpp
      host/windows/app.cpp
      host/windows/base_view.cpp
      host/windows/key.cpp
      host/windows/window.cpp
      host/windows/drag_and_drop.cpp
   )
endif()

source_group("Source Files\\elements"
   FILES
   ${ELEMENTS_SOURCES}
)

source_group("Source Files\\elements_host"
   FILES
   ${ELEMENTS_HOST}
)

source_group("Header Files\\elements"
   FILES
   ${ELEMENTS_HEADERS}
)

add_library(elements STATIC)

target_sources(elements
   PRIVATE ${ELEMENTS_SOURCES} ${ELEMENTS_HOST}
   PUBLIC ${ELEMENTS_HEADERS}
)

target_include_directories(elements PUBLIC include)
target_compile_features(elements PUBLIC cxx_std_20)

if(ELEMENTS_ENABLE_LTO)
   set_target_properties(elements PROPERTIES INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

###############################################################################
# Get rid of certain warnings, and make msvc support utf-8 without adding bom

target_compile_options(elements PRIVATE
   $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra -Wpedantic -ftemplate-backtrace-limit=0>
   $<$<CXX_COMPILER_ID:Clang>:-Wall -Wpedantic -ftemplate-backtrace-limit=0>
   $<$<CXX_COMPILER_ID:MSVC>:/W4 /wd4244 /wd4305 /wd4996 /wd4267 /wd4018 /wd4456 /utf-8>
)

if (MSVC)
   # Kinda silly to have to do this: https://bit.ly/2ZXjzzn
   STRING(REGEX REPLACE "/W3" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
endif()

###############################################################################
# Cycfi Infra

FetchContent_Declare(
  cycfi_infra
  GIT_REPOSITORY https://github.com/cycfi/infra.git
  GIT_TAG master
  GIT_SUBMODULES_RECURSE ON
)

FetchContent_MakeAvailable(cycfi_infra)
target_link_libraries(elements PUBLIC cycfi::infra)

###############################################################################
# Dependencies: ThorVG (submodule), FreeType (vcpkg), HarfBuzz (via ThorVG)
###############################################################################

# ThorVG loader options
option(TVG_BUILD_SHARED "Build shared library" OFF)
option(TVG_LOADER_SVG "Enable SVG Loader" OFF)
option(TVG_LOADER_PNG "Enable PNG Loader" ON)
option(TVG_LOADER_JPG "Enable JPG Loader" ON)
option(TVG_LOADER_LOTTIE "Enable Lottie Loader" OFF)
option(TVG_LOADER_TTF "Enable TTF Loader" OFF)
option(TVG_LOADER_FT "Enable FreeType+HarfBuzz multilingual text loader (mutually exclusive with TVG_LOADER_TTF)" ON)
option(TVG_LOADER_WEBP "Enable WebP Loader" OFF)
option(TVG_LOG "Enable log message" OFF)
option(TVG_FILE_IO "Enable File IO calls" ON)
# add_subdirectory で使うため install(EXPORT) を無効化。harfbuzz など
# 非エクスポートな下流ターゲットが export set に巻き込まれて失敗するのを避ける。
set(TVG_INSTALL OFF CACHE BOOL "Generate install/export rules" FORCE)

# --- ThorVG (git submodule at external/thorvg, branch: cmake) ---
# TVG_LOADER_FT=ON のとき ThorVG 側で HarfBuzz を FetchContent する
# (external/thorvg/CMakeLists.txt 参照)。`harfbuzz` / `Freetype::Freetype`
# の target は add_subdirectory 後に利用可能になる。
# 親プロジェクトが thorvg ターゲットを既に作っているならそれを再利用する
# (krkrz_dev umbrella で他サブが先に呼ぶケース)。
if(NOT TARGET thorvg)
   set(_thorvg_dir "${PROJECT_SOURCE_DIR}/external/thorvg")
   if (NOT EXISTS "${_thorvg_dir}/CMakeLists.txt")
      message(FATAL_ERROR
         "ThorVG submodule is not initialized at ${_thorvg_dir}.\n"
         "Run: git submodule update --init --recursive external/thorvg")
   endif()
   add_subdirectory("${_thorvg_dir}" "${CMAKE_CURRENT_BINARY_DIR}/thorvg" EXCLUDE_FROM_ALL)
   target_include_directories(elements PUBLIC "${_thorvg_dir}/inc")
endif()
target_link_libraries(elements PUBLIC thorvg)

# --- FreeType / HarfBuzz ---
# どちらも ThorVG 側で setup 済み。elements も glyph_layout_ft.cpp 等で
# 直接 API を呼ぶので PUBLIC でリンクする。
find_package(freetype CONFIG REQUIRED)
target_link_libraries(elements PUBLIC freetype harfbuzz)

###############################################################################
# Asio headers

FetchContent_Declare(
  asio
  GIT_REPOSITORY https://github.com/chriskohlhoff/asio.git
  GIT_TAG asio-1-36-0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(asio)

target_include_directories(elements PUBLIC ${asio_SOURCE_DIR}/asio/include)

###############################################################################
# Global options

# Common definitions
target_compile_definitions(elements PUBLIC
   BOOST_DATE_TIME_NO_LIB
   BOOST_REGEX_NO_LIB
)

###############################################################################
# Host-specific definitions and linking

if (ELEMENTS_HOST_UI_LIBRARY STREQUAL "sdl")

   message(STATUS "Using SDL3 as host UI library")

   # SDL3: 親 CMakeLists で既に SDL3::SDL3 が提供されていればそれを使う。
   # 単体ビルド時のみ FetchContent でダウンロードする。
   if (NOT TARGET SDL3::SDL3)
      message(STATUS "SDL3::SDL3 target not found, fetching SDL3 via FetchContent")
      set(SDL_SHARED ON CACHE BOOL "Build SDL shared library")
      set(SDL_STATIC OFF CACHE BOOL "Build SDL static library")

      include(FetchContent)
      FetchContent_Declare(
         SDL3
         GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
         GIT_TAG release-3.4.0
      )
      FetchContent_MakeAvailable(SDL3)
   else()
      message(STATUS "Using SDL3::SDL3 target provided by parent project")
   endif()

   target_compile_definitions(elements PUBLIC ELEMENTS_HOST_UI_LIBRARY_SDL)
   target_link_libraries(elements PUBLIC SDL3::SDL3)
   if (WIN32)
      target_compile_definitions(elements PUBLIC WIN32_LEAN_AND_MEAN NOMINMAX _UNICODE)
   endif()
else()

   message(STATUS "Using Win32 API as host UI library")

   # Win32 host
   target_compile_definitions(elements PUBLIC
      WIN32_LEAN_AND_MEAN
      NOMINMAX
      _UNICODE
      ELEMENTS_HOST_UI_LIBRARY_WIN32
   )
endif()

if (WIN32 AND NOT MSVC)
   find_package(Threads)
   target_link_libraries(elements PUBLIC Threads::Threads ws2_32)
endif()

if (WIN32)
   target_link_libraries(elements PUBLIC Shcore)
endif()

add_library(cycfi::elements ALIAS elements)
