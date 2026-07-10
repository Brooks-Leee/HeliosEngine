cmake_minimum_required(VERSION 3.25)
project(HeliosEngine VERSION 0.1.0 LANGUAGES CXX)

# C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build options
option(HELIOS_BUILD_DX12 "Build DirectX 12 backend" ON)
option(HELIOS_BUILD_VULKAN "Build Vulkan backend" ON)
option(HELIOS_BUILD_EDITOR "Build editor" OFF)
option(HELIOS_BUILD_TESTS "Build tests" OFF)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Platform detection
if(WIN32)
    add_compile_definitions(HELIOS_PLATFORM_WINDOWS)
    # Disable min/max macros from windows.h
    add_compile_definitions(NOMINMAX WIN32_LEAN_AND_MEAN)
else()
    message(FATAL_ERROR "HeliosEngine currently only supports Windows")
endif()

# Render backends
if(HELIOS_BUILD_DX12)
    add_compile_definitions(HELIOS_RENDER_DX12)
endif()
if(HELIOS_BUILD_VULKAN)
    add_compile_definitions(HELIOS_RENDER_VULKAN)
endif()

# Editor
if(HELIOS_BUILD_EDITOR)
    add_compile_definitions(HELIOS_EDITOR)
endif()

# Source
add_subdirectory(src)
