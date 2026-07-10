# Engine core library
add_subdirectory(core)

# Main executable
add_executable(HeliosEngine main.cpp)
target_link_libraries(HeliosEngine PRIVATE HeliosCore)

# Shader directory relative to exe
target_compile_definitions(HeliosEngine PRIVATE
    HELIOS_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    HELIOS_ASSET_DIR="${CMAKE_SOURCE_DIR}/assets"
)
