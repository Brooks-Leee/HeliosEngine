# HeliosCore — Engine core static library
add_library(HeliosCore STATIC)

target_sources(HeliosCore PRIVATE
    # Will be populated as modules are added
)

target_include_directories(HeliosCore PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# DX12
if(HELIOS_BUILD_DX12)
    target_link_libraries(HeliosCore PUBLIC
        d3d12
        dxgi
        d3dcompiler
    )
endif()

# Vulkan
if(HELIOS_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(HeliosCore PUBLIC
        Vulkan::Vulkan
    )
endif()

# Windows
target_link_libraries(HeliosCore PUBLIC
    # Common libs
)
