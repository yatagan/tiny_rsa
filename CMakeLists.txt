cmake_minimum_required(VERSION 3.28)

#
# This file is generated only once,
# and is not re-generated if converter is called multiple times.
#
# User is free to modify the file as much as necessary
#

# Setup compiler settings
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Must be set before CXX is enabled (project() below enables it by default).
# The UUID is tied to this exact CMake version (4.3.1, bundled with the
# STM32Cube VS Code extension) -- it will need updating if that CMake bundle
# is ever upgraded.
set(CMAKE_CXX_MODULE_STD ON)
set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD "451f2fe2-a8a2-47c3-bc32-94786d8fc91b")


# Define the build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# Set the project name
set(CMAKE_PROJECT_NAME tiny_rta)

# Enable compile command to ease indexing with e.g. clangd
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

# Core project settings
project(${CMAKE_PROJECT_NAME})
message("Build type: " ${CMAKE_BUILD_TYPE})

# Enable CMake support for ASM and C languages
enable_language(C ASM)

# Create an executable object type
add_executable(${CMAKE_PROJECT_NAME})
# tiny_rta itself has no C++ sources (see dummy_lib below for those), so it
# never picks up -stdlib=libc++ -- opt it out of CXX_MODULE_STD, which
# CMAKE_CXX_MODULE_STD would otherwise default onto every target and which
# then fails toolchain validation for a target with no C++ standard library.
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES CXX_MODULE_STD OFF)

# Add STM32CubeMX generated sources
add_subdirectory(cmake/stm32cubemx)

# Dummy C++23 module demonstrating `import std;` support
add_library(dummy_lib)
target_sources(dummy_lib
    PUBLIC FILE_SET my_modules TYPE CXX_MODULES FILES
        App/dummy.cppm
    PRIVATE
        App/app_cpp.cpp
)
target_include_directories(dummy_lib PRIVATE App)

# MAX7219 4-in-1 LED matrix driver + bar-graph demo (C++23 modules, import std).
# Links stm32cubemx so the modules' global module fragments can #include the
# HAL headers and clang-scan-deps sees the same -I/-D flags.
add_library(max7219_lib)
target_sources(max7219_lib
    PUBLIC FILE_SET max7219_modules TYPE CXX_MODULES FILES
        App/max7219.cppm
        App/max7219_demo.cppm
    PRIVATE
        App/app_max7219.cpp
)
target_include_directories(max7219_lib PRIVATE App)
target_link_libraries(max7219_lib PRIVATE stm32cubemx)
# Build the display driver for size, not speed. Its bit-bang SPI loops unroll
# into ~2.8 KB of straight-line stores under the global -O3, and none of it is
# hot: the MAX7219 refresh is gated by the display, not by the CPU. Target
# options come after CMAKE_CXX_FLAGS_<CONFIG>, so this -Os wins over -O3.
target_compile_options(max7219_lib PRIVATE $<$<CONFIG:Release>:-Os>)

# Link directories setup
target_link_directories(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined library search paths
)

# Add sources to executable
target_sources(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user sources here
)

# Add include paths
target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    App
    # Add user defined include paths
)

# Add project symbols (macros)
target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    # Add user defined symbols
)

# Remove wrong libob.a library dependency when using cpp files
list(REMOVE_ITEM CMAKE_C_IMPLICIT_LINK_LIBRARIES ob)

# Add linked libraries
target_link_libraries(${CMAKE_PROJECT_NAME}
    stm32cubemx
    dummy_lib
    max7219_lib

    # Add user defined libraries
)
