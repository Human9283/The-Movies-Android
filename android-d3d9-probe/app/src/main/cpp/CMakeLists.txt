cmake_minimum_required(VERSION 3.22.1)
project(the_movies_d3d9_probe C CXX)

if(NOT DEFINED DXVK_SOURCE_DIR OR NOT EXISTS "${DXVK_SOURCE_DIR}/include/native/directx/d3d9.h")
    message(FATAL_ERROR "DXVK_SOURCE_DIR must point to the pinned DXVK Native Android checkout")
endif()
if(NOT DEFINED SDL2_INCLUDE_DIR OR NOT EXISTS "${SDL2_INCLUDE_DIR}/SDL.h")
    message(FATAL_ERROR "SDL2_INCLUDE_DIR must point to SDL2/include")
endif()

set(SDL2_LIBRARY "${SDL2_LIB_DIR}/libSDL2.so")
if(NOT EXISTS "${SDL2_LIBRARY}")
    message(FATAL_ERROR "SDL2 shared library not found at ${SDL2_LIBRARY}")
endif()
find_library(ANDROID_LIBRARY android REQUIRED)
find_library(LOG_LIBRARY log REQUIRED)
find_library(DL_LIBRARY dl REQUIRED)

add_library(main SHARED main.cpp)
target_compile_features(main PRIVATE cxx_std_17)
target_compile_definitions(main PRIVATE SDL_MAIN_HANDLED NOMINMAX)
target_include_directories(main PRIVATE
    "${SDL2_INCLUDE_DIR}"
    "${DXVK_SOURCE_DIR}/include/native/directx"
    "${DXVK_SOURCE_DIR}/include/native/windows"
    "${DXVK_SOURCE_DIR}/include/native"
)
target_compile_options(main PRIVATE -Wall -Wextra -Wno-unused-parameter)
target_link_libraries(main PRIVATE ${SDL2_LIBRARY} ${ANDROID_LIBRARY} ${LOG_LIBRARY} ${DL_LIBRARY})
