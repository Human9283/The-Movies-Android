cmake_minimum_required(VERSION 3.22.1)
project(the_movies_vulkan_probe C)

add_library(main SHARED main.c
    "${ANDROID_NDK}/sources/android/native_app_glue/android_native_app_glue.c")

target_include_directories(main PRIVATE
    "${ANDROID_NDK}/sources/android/native_app_glue")

find_library(android-lib android REQUIRED)
find_library(log-lib log REQUIRED)
find_library(vulkan-lib vulkan REQUIRED)

target_link_libraries(main PRIVATE ${android-lib} ${log-lib} ${vulkan-lib})
target_compile_options(main PRIVATE -Wall -Wextra -Werror=return-type)
