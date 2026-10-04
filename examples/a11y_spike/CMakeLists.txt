cmake_minimum_required(VERSION 3.16.0)
project(A11ySpike LANGUAGES C CXX)

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

# accesskit-c の prebuilt リリース。 ソース配布の CMakeLists.txt は cargo で
# 建てようとするので add_subdirectory させず (SOURCE_SUBDIR に無い名前)、
# 同梱の accesskit-config.cmake だけを読む。
include(FetchContent)
FetchContent_Declare(
   accesskit_c
   URL      https://github.com/AccessKit/accesskit-c/releases/download/0.23.1/accesskit-c-0.23.1.zip
   URL_HASH SHA256=35b7ca8a6f1e038b5da35e1e9e5a0adaed9bfcf21e1496d29598fbbadcc7043f
   SOURCE_SUBDIR _no_cmake_
)
FetchContent_MakeAvailable(accesskit_c)
include(${accesskit_c_SOURCE_DIR}/accesskit-config.cmake)

set(ELEMENTS_APP_PROJECT "A11ySpike")
set(ELEMENTS_APP_TITLE "A11y Spike")
set(ELEMENTS_APP_COPYRIGHT "Copyright (c) 2026")
set(ELEMENTS_APP_ID "com.cycfi.a11y_spike")
set(ELEMENTS_APP_VERSION "1.0")

set(ELEMENTS_APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

include(AppIcon)
include(ElementsConfigApp)

target_link_libraries(${ELEMENTS_APP_PROJECT} PRIVATE accesskit)
if (WIN32)
   target_link_libraries(${ELEMENTS_APP_PROJECT} PRIVATE comctl32)
endif()
