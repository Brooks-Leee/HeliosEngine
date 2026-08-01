# HeliosCore — Engine core static library
add_library(HeliosCore STATIC)

target_sources(HeliosCore PRIVATE
    Platform/Window.h
    Platform/Window.cpp
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
    # 让 cpp 能拼出 shader .spv 的绝对路径（与 HeliosEngine target 保持一致）
    target_compile_definitions(HeliosCore PRIVATE
        HELIOS_SHADER_DIR="${CMAKE_SOURCE_DIR}/shaders"
    )
    target_sources(HeliosCore PRIVATE
        Render/Vulkan/VulkanRenderer.h
        Render/Vulkan/VulkanRenderer.cpp
    )

    # --- 用 glslc 自动把 GLSL 编译成 SPIR-V（.spv）---
    # .spv 是构建产物，已 gitignore；改 .vert/.frag 后重新 build 会自动重编。
    # 运行时按 HELIOS_SHADER_DIR 读，所以输出放回 shaders/vulkan/ 源码旁。
    find_program(GLSL_COMPILER glslc HINTS "$ENV{VULKAN_SDK}/Bin" "$ENV{VULKAN_SDK}/bin")
    if(NOT GLSL_COMPILER)
        message(FATAL_ERROR "glslc 未找到——请安装 Vulkan SDK 或将其加入 PATH")
    endif()

    set(VULKAN_SHADER_SOURCES
        "${CMAKE_SOURCE_DIR}/shaders/vulkan/triangle.vert"
        "${CMAKE_SOURCE_DIR}/shaders/vulkan/triangle.frag"
        "${CMAKE_SOURCE_DIR}/shaders/vulkan/post.vert"
        "${CMAKE_SOURCE_DIR}/shaders/vulkan/post.frag"
    )
    foreach(SHADER_SRC ${VULKAN_SHADER_SOURCES})
        set(SHADER_OUT "${SHADER_SRC}.spv")
        add_custom_command(
            OUTPUT "${SHADER_OUT}"
            COMMAND "${GLSL_COMPILER}" "${SHADER_SRC}" -o "${SHADER_OUT}"
            DEPENDS "${SHADER_SRC}"
            COMMENT "编译 shader: ${SHADER_SRC} -> .spv"
        )
        list(APPEND VULKAN_SHADER_SPV_OUTPUTS "${SHADER_OUT}")
    endforeach()

    # 让 HeliosCore 依赖 shader 构建目标，build 时自动先编 shader
    add_custom_target(HeliosShaders DEPENDS ${VULKAN_SHADER_SPV_OUTPUTS})
    add_dependencies(HeliosCore HeliosShaders)
endif()

# Windows system libraries
if(WIN32)
    target_link_libraries(HeliosCore PUBLIC
        user32       # RegisterClassEx, CreateWindowEx, PeekMessage…
        gdi32        # GDI drawing (may be needed later)
    )
endif()
