###############################################################################
#  Copyright (c) 2016-2023 Joel de Guzman
#
#  Distributed under the MIT License (https://opensource.org/licenses/MIT)
###############################################################################
cmake_minimum_required(VERSION 3.9.6...3.15.0)

project(elements LANGUAGES C CXX)

message(STATUS "CMAKE_C_COMPILER ${CMAKE_C_COMPILER}")
message(STATUS "CMAKE_C_COMPILER_ID ${CMAKE_C_COMPILER_ID}")
message(STATUS "CMAKE_C_COMPILER_VERSION ${CMAKE_C_COMPILER_VERSION}")
message(STATUS "CMAKE_CXX_COMPILER ${CMAKE_CXX_COMPILER}")
message(STATUS "CMAKE_CXX_COMPILER_ID ${CMAKE_CXX_COMPILER_ID}")
message(STATUS "CMAKE_CXX_COMPILER_VERSION ${CMAKE_CXX_COMPILER_VERSION}")

set(DEFAULT_BUILD_TYPE "Release")
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to '${DEFAULT_BUILD_TYPE}' as none was specified.")
    set(CMAKE_BUILD_TYPE "${DEFAULT_BUILD_TYPE}" CACHE
        STRING "Choose the type of build." FORCE)
    # Set the possible values of build type for cmake-gui
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
        "Debug" "Release" "MinSizeRel" "RelWithDebInfo")
endif()

if(ELEMENTS_ENABLE_LTO)
    include(CheckIPOSupported)
    check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)

    if(IPO_SUPPORTED)
        message(STATUS "Link-time optimization enabled for elements")
	else()
	    message(FATAL_ERROR "Link-time optimization enabled for elements but it is not supported: ${IPO_ERROR}")
    endif()
else()
    message(STATUS "Link-time optimization disabled for elements")
endif()

###############################################################################
# UI Libraries (Windows only — 最終的にSDLで統一予定)

set(ELEMENTS_HOST_UI_LIBRARY "sdl" CACHE STRING "Host UI library (sdl or win32)")

message(STATUS "building elements with ${ELEMENTS_HOST_UI_LIBRARY} host UI library")

###############################################################################

