# AccessKit + SDL3 の最小疎通 (Elements 無し)。 macOS / Linux で
# «表示済みのウィンドウに後から付けられるか» と «スレッドの扱い» を見る。
#
#   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
#   cmake --build build
cmake_minimum_required(VERSION 3.20)
project(A11ySdlMinimal LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
set(SDL_TEST_LIBRARY OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
   SDL3
   GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
   GIT_TAG        release-3.4.0
   GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(SDL3)

FetchContent_Declare(
   accesskit_c
   URL      https://github.com/AccessKit/accesskit-c/releases/download/0.23.1/accesskit-c-0.23.1.zip
   URL_HASH SHA256=35b7ca8a6f1e038b5da35e1e9e5a0adaed9bfcf21e1496d29598fbbadcc7043f
   SOURCE_SUBDIR _no_cmake_
   DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(accesskit_c)
if (APPLE AND NOT CMAKE_OSX_ARCHITECTURES)
   set(CMAKE_OSX_ARCHITECTURES ${CMAKE_HOST_SYSTEM_PROCESSOR})
endif()
include(${accesskit_c_SOURCE_DIR}/accesskit-config.cmake)

add_executable(a11y_sdl_minimal main.cpp)
target_link_libraries(a11y_sdl_minimal PRIVATE SDL3::SDL3 accesskit)
if (WIN32)
   target_link_libraries(a11y_sdl_minimal PRIVATE comctl32)
endif()
