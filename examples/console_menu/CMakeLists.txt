cmake_minimum_required(VERSION 3.16.0)
project(ConsoleMenu LANGUAGES C CXX)

if (NOT ELEMENTS_ROOT)
   message(FATAL_ERROR "ELEMENTS_ROOT is not set")
endif()

get_filename_component(ELEMENTS_ROOT "${ELEMENTS_ROOT}" ABSOLUTE)
set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${ELEMENTS_ROOT}/cmake")

if (NOT ELEMENTS_BUILD_EXAMPLES)
   include(ElementsConfigCommon)
   set(ELEMENTS_BUILD_EXAMPLES OFF)
   add_subdirectory(${ELEMENTS_ROOT} elements)
endif()

set(ELEMENTS_APP_PROJECT "ConsoleMenu")
set(ELEMENTS_APP_TITLE "Console Menu")
set(ELEMENTS_APP_COPYRIGHT "Copyright (c) 2026")
set(ELEMENTS_APP_ID "com.cycfi.console_menu")
set(ELEMENTS_APP_VERSION "1.0")

set(ELEMENTS_APP_SOURCES
   ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
)

include(AppIcon)
include(ElementsConfigApp)

# pad_icon: Kenney pack 配置の絶対パスを compile def で注入。
# 配置が無くても build できるよう pad_icon 側で fallback プレースホルダを描く。
target_compile_definitions(${ELEMENTS_APP_PROJECT} PRIVATE
   CONSOLE_MENU_PAD_BASE_DIR="${ELEMENTS_ROOT}/resources/kenny_input_prompts"
)
