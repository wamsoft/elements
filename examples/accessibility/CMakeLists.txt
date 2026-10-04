cmake_minimum_required(VERSION 3.16.0)
project(Accessibility LANGUAGES C CXX)

if (NOT ELEMENTS_ROOT)
   message(FATAL_ERROR "ELEMENTS_ROOT is not set")
endif()

get_filename_component(ELEMENTS_ROOT "${ELEMENTS_ROOT}" ABSOLUTE)
set (CMAKE_MODULE_PATH "${CMAKE_MODULE_PATH};${ELEMENTS_ROOT}/cmake")

if (NOT ELEMENTS_BUILD_EXAMPLES)
   include(ElementsConfigCommon)
   set(ELEMENTS_BUILD_EXAMPLES OFF)
   set(ELEMENTS_A11Y_ACCESSKIT ON CACHE BOOL "" FORCE)
   add_subdirectory(${ELEMENTS_ROOT} elements)
endif()

set(ELEMENTS_APP_PROJECT "Accessibility")
set(ELEMENTS_APP_TITLE "Accessibility")
set(ELEMENTS_APP_COPYRIGHT "Copyright (c) 2026")
set(ELEMENTS_APP_ID "com.cycfi.accessibility")
set(ELEMENTS_APP_VERSION "1.0")

set(ELEMENTS_APP_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)

include(AppIcon)
include(ElementsConfigApp)

target_link_libraries(${ELEMENTS_APP_PROJECT} PRIVATE elements_a11y_accesskit)
