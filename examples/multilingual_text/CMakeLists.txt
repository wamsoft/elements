cmake_minimum_required(VERSION 3.16.0)
project(MultilingualText LANGUAGES C CXX)

if (NOT ELEMENTS_ROOT)
   message(FATAL_ERROR "ELEMENTS_ROOT is not set")
endif()

# Make sure ELEMENTS_ROOT is an absolute path to add to the CMake module path
get_filename_component(ELEMENTS_ROOT "${ELEMENTS_ROOT}" ABSOLUTE)
set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${ELEMENTS_ROOT}/cmake")

# If we are building outside the project, you need to set ELEMENTS_ROOT:
if (NOT ELEMENTS_BUILD_EXAMPLES)
   include(ElementsConfigCommon)
   set(ELEMENTS_BUILD_EXAMPLES OFF)
   add_subdirectory(${ELEMENTS_ROOT} elements)
endif()

set(ELEMENTS_APP_PROJECT "MultilingualText")
set(ELEMENTS_APP_TITLE "Multilingual Text")
set(ELEMENTS_APP_COPYRIGHT "Copyright (c) 2016-2023 Joel de Guzman")
set(ELEMENTS_APP_ID "com.cycfi.multilingual-text")
set(ELEMENTS_APP_VERSION "1.0")

set(ELEMENTS_APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

# Pull in CJK + emoji fallback fonts in addition to the default Roboto/OpenSans set
set(ELEMENTS_APP_RESOURCES
   ${ELEMENTS_ROOT}/resources/fonts/NotoSansJP-Regular.otf
   ${ELEMENTS_ROOT}/resources/fonts/NotoSansSC-Regular.otf
   ${ELEMENTS_ROOT}/resources/fonts/NotoSansTC-Regular.otf
   ${ELEMENTS_ROOT}/resources/fonts/NotoEmoji-Regular.ttf
)

include(ElementsConfigApp)
