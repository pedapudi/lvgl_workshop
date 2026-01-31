# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.22)

# Add 'external' to component search paths so ESP-IDF finds lvgl_cpp directly
set(EXTRA_COMPONENT_DIRS "components" "external")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(animation_workshop)

# Fix for ThorVG build warnings treated as errors
if(TARGET __idf_lvgl__lvgl)
    target_compile_options(__idf_lvgl__lvgl PUBLIC 
        "-Wno-error=type-limits"
        "-Wno-error=format=" 
        "-Wno-error=unused-variable"
    )
endif()
