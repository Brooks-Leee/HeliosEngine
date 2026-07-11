# Vulkan Hello Triangle — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在窗口中显示一个 Vulkan 渲染的三角形，通过 validation layer 验证，退出时资源正确释放。

**Architecture:** 所有 Vulkan 代码集中在 `VulkanRenderer.h/.cpp` 两个文件中——不拆模块。先跑通完整流程（Instance → Device → SwapChain → Pipeline → Command → Draw），后续再按 spec 重构拆分。Shader 使用硬编码 SPIR-V 字节数组，不引入编译工具链依赖。

**Tech Stack:** C++20, Vulkan 1.4 (传统 RenderPass 路径), vulkan.hpp, Win32 HWND, glslc

## Global Constraints

- 所有 Vulkan 对象放在 `VulkanRenderer` 单个类中
- 使用 `vulkan.hpp` (C++ bindings)，`vk::UniqueHandle` 做 RAII
- 异常处理：直接用 `vk::resultCheck()` 抛异常
- Debug 构建开启 `VK_LAYER_KHRONOS_validation`
- 硬编码 SPIR-V，不依赖运行时 shader 编译
- 命名空间：`Helios`

---

### Task 1: 编写 GLSL Shader 并编译为 SPIR-V

**Files:**
- Create: `shaders/vulkan/triangle.vert`
- Create: `shaders/vulkan/triangle.frag`

**Interfaces:**
- Produces: GLSL 源码文件，供 glslc 编译为 SPIR-V 字节数组嵌入 C++

- [ ] **Step 1: 编写顶点着色器 `shaders/vulkan/triangle.vert`**

```glsl
#version 450

// 硬编码三个顶点位置——不需要 vertex buffer
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
```

- [ ] **Step 2: 编写片元着色器 `shaders/vulkan/triangle.frag`**

```glsl
#version 450

layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(0.0, 0.4, 0.8, 1.0);
}
```

- [ ] **Step 3: 用 glslc 编译为 SPIR-V**

```bash
"C:/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/vulkan/triangle.vert -o shaders/vulkan/triangle.vert.spv
"C:/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/vulkan/triangle.frag -o shaders/vulkan/triangle.frag.spv
```

- [ ] **Step 4: 将 SPIR-V 二进制转为 C++ 头文件**

用 xxd 风格输出：读取 .spv 文件，生成 `static const uint32_t kVertSPIRV[] = {...}` 和 `static const uint32_t kFragSPIRV[] = {...}` 形式的头文件。

写一个临时的 Python 脚本来做这件事（之后 CMake 可以自动化），或者手动用 Bash：

```bash
# 生成 C++ 头文件 shaders/vulkan/triangle_shaders.h
echo '#pragma once' > shaders/vulkan/triangle_shaders.h
echo '#include <cstdint>' >> shaders/vulkan/triangle_shaders.h
echo '' >> shaders/vulkan/triangle_shaders.h
echo '// Auto-generated SPIR-V arrays from triangle.vert / triangle.frag' >> shaders/vulkan/triangle_shaders.h
echo '' >> shaders/vulkan/triangle_shaders.h
xxd -i shaders/vulkan/triangle.vert.spv | sed 's/unsigned char/static const uint32_t/g' | sed 's/_spv\[\]/_vert_spv\[\]/g' | sed 's/_len/_vert_len/g' >> shaders/vulkan/triangle_shaders.h
xxd -i shaders/vulkan/triangle.frag.spv | sed 's/unsigned char/static const uint32_t/g' | sed 's/_spv\[\]/_frag_spv\[\]/g' | sed 's/_len/_frag_len/g' >> shaders/vulkan/triangle_shaders.h
```

> ⚠️ 注意：`xxd -i` 生成的是 `unsigned char` 数组和 `_len`，需要改成 `uint32_t`，而且 SPIR-V 的字节数必须是 4 的倍数。如果 `xxd` 不可用，用 Python 脚本替代。

用 Python 更可靠：

```bash
python -c "
import pathlib
for name in ('triangle.vert', 'triangle.frag'):
    spv = pathlib.Path(f'shaders/vulkan/{name}.spv').read_bytes()
    assert len(spv) % 4 == 0, f'{name}.spv size not multiple of 4'
    words = []
    for i in range(0, len(spv), 4):
        words.append(str(int.from_bytes(spv[i:i+4], 'little')))
    suffix = 'vert' if 'vert' in name else 'frag'
    print(f'static const uint32_t k{suffix.capitalize()}SPIRV[] = {{')
    print(', '.join(words))
    print(f'}}; // {len(words)} words')
" > shaders/vulkan/triangle_shaders.h
```

> 手动把生成的内容包装成：

```cpp
#pragma once
#include <cstdint>

// Auto-generated SPIR-V arrays from triangle.vert / triangle.frag

static const uint32_t kVertSPIRV[] = {
    // ... generated words ...
};
static const uint32_t kFragSPIRV[] = {
    // ... generated words ...
};
```

- [ ] **Step 5: 验证编译**

确认 shader 能正常编译且无警告：

```bash
"C:/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/vulkan/triangle.vert -o shaders/vulkan/triangle.vert.spv && echo "Vert OK"
"C:/VulkanSDK/1.4.350.0/Bin/glslc.exe" shaders/vulkan/triangle.frag -o shaders/vulkan/triangle.frag.spv && echo "Frag OK"
```

Expected: `Vert OK` + `Frag OK`

---

### Task 2: 创建 VulkanRenderer 骨架 + VulkanDevice 初始化

**Files:**
- Create: `src/core/Render/Vulkan/VulkanRenderer.h` — 类声明，所有 Vulkan 对象作为成员
- Create: `src/core/Render/Vulkan/VulkanRenderer.cpp` — Device 初始化部分
- Modify: `src/core/CMakeLists.txt` — 添加 Vulkan 源文件

**Interfaces:**
- Produces: `class VulkanRenderer` with `Initialize(HWND, width, height)`, `Render()`, `Shutdown()`

- [ ] **Step 1: 更新 CMakeLists.txt 添加文件**

`src/core/CMakeLists.txt`: 在 `if(HELIOS_BUILD_VULKAN)` 块中追加：

```cmake
if(HELIOS_BUILD_VULKAN)
    target_sources(HeliosCore PRIVATE
        Render/Vulkan/VulkanRenderer.h
        Render/Vulkan/VulkanRenderer.cpp
    )
endif()
```

- [ ] **Step 2: 创建 VulkanRenderer.h 头文件**

```cpp
#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.hpp>
#include <windows.h>
#include <vector>

namespace Helios {

class VulkanRenderer {
public:
    void Initialize(HWND hwnd, int width, int height);
    void Render();
    void Shutdown();

private:
    // --- Debug ---
#ifndef NDEBUG
    void SetupDebugMessenger();
    vk::UniqueDebugUtilsMessengerEXT m_debugMessenger;
#endif

    // --- Device ---
    vk::UniqueInstance       m_instance;
    vk::PhysicalDevice       m_physicalDevice;
    vk::UniqueDevice         m_device;
    uint32_t                 m_graphicsQueueFamily = 0;
    vk::Queue                m_graphicsQueue;

    // --- Surface & SwapChain ---
    vk::UniqueSurfaceKHR     m_surface;
    vk::UniqueSwapchainKHR   m_swapChain;
    vk::Format               m_swapChainFormat;
    vk::Extent2D             m_swapChainExtent;
    std::vector<vk::Image>   m_swapChainImages;
    std::vector<vk::UniqueImageView> m_swapChainImageViews;

    // --- Pipeline ---
    vk::UniqueRenderPass     m_renderPass;
    vk::UniquePipelineLayout m_pipelineLayout;
    vk::UniquePipeline       m_pipeline;
    std::vector<vk::UniqueFramebuffer> m_framebuffers;

    // --- Command ---
    vk::UniqueCommandPool    m_commandPool;
    vk::CommandBuffer        m_commandBuffer;

    // --- Sync ---
    vk::UniqueSemaphore      m_imageAvailableSemaphore;
    vk::UniqueSemaphore      m_renderFinishedSemaphore;
};

} // namespace Helios
```

- [ ] **Step 3: 实现 VulkanDevice 初始化**

`VulkanRenderer.cpp` — 第一部分：`Initialize()` 的 Device 创建。

```cpp
#include "VulkanRenderer.h"
#include "Vulkan/triangle_shaders.h"  // kVertSPIRV, kFragSPIRV

#include <iostream>
#include <set>

namespace Helios {

namespace {
    // 验证所有 required validation layers 都可用
    bool CheckValidationLayerSupport(const std::vector<const char*>& requiredLayers) {
        auto availableLayers = vk::enumerateInstanceLayerProperties();
        for (auto* required : requiredLayers) {
            bool found = false;
            for (const auto& layer : availableLayers) {
                if (strcmp(layer.layerName, required) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        return true;
    }
}

void VulkanRenderer::Initialize(HWND hwnd, int width, int height) {
    // ================================================================
    // 1. Instance 创建
    // ================================================================

    // 1a. Application info
    vk::ApplicationInfo appInfo;
    appInfo.pApplicationName = "HeliosEngine";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "HeliosEngine";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_4;  // SDK 1.4.350.0，渲染走传统 RenderPass

    // 1b. Required extensions: surface from HWND
    std::vector<const char*> instanceExtensions = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
    };

    // 1c. Validation layers (Debug only)
    std::vector<const char*> validationLayers;
#ifndef NDEBUG
    instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    validationLayers = { "VK_LAYER_KHRONOS_validation" };
    if (!CheckValidationLayerSupport(validationLayers)) {
        std::cerr << "[Vulkan] Validation layers requested but not available!\n";
        validationLayers.clear();
    }
#endif

    vk::InstanceCreateInfo instanceCreateInfo;
    instanceCreateInfo.pApplicationInfo = &appInfo;
    instanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
    instanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
    instanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
    instanceCreateInfo.ppEnabledLayerNames = validationLayers.data();

    m_instance = vk::createInstanceUnique(instanceCreateInfo);

    // 1d. Debug messenger (Debug only)
#ifndef NDEBUG
    SetupDebugMessenger();
#endif

    // ================================================================
    // 2. Physical Device 选择
    // ================================================================

    auto physicalDevices = m_instance->enumeratePhysicalDevices();
    if (physicalDevices.empty()) {
        throw std::runtime_error("No Vulkan-capable GPU found!");
    }

    // 优先独立显卡，fallback 到第一个可用设备
    m_physicalDevice = nullptr;
    for (const auto& device : physicalDevices) {
        auto props = device.getProperties();
        if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
            m_physicalDevice = device;
            std::cout << "[Vulkan] Selected discrete GPU: " << props.deviceName << "\n";
            break;
        }
    }
    if (!m_physicalDevice) {
        m_physicalDevice = physicalDevices[0];
        auto props = m_physicalDevice.getProperties();
        std::cout << "[Vulkan] Fallback to: " << props.deviceName << "\n";
    }

    // 找到 graphics queue family
    auto queueFamilies = m_physicalDevice.getQueueFamilyProperties();
    bool foundGraphicsQueue = false;
    for (uint32_t i = 0; i < queueFamilies.size(); i++) {
        if (queueFamilies[i].queueFlags & vk::QueueFlagBits::eGraphics) {
            m_graphicsQueueFamily = i;
            foundGraphicsQueue = true;
            break;
        }
    }
    if (!foundGraphicsQueue) {
        throw std::runtime_error("No graphics queue family found!");
    }

    // ================================================================
    // 3. Logical Device 创建
    // ================================================================

    // 验证 swapchain extension 支持
    auto deviceExtensions = m_physicalDevice.enumerateDeviceExtensionProperties();
    std::set<std::string> requiredDeviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };
    for (const auto& ext : deviceExtensions) {
        requiredDeviceExtensions.erase(std::string(ext.extensionName));
    }
    if (!requiredDeviceExtensions.empty()) {
        throw std::runtime_error("Device does not support swapchain extension!");
    }

    float queuePriority = 1.0f;
    vk::DeviceQueueCreateInfo queueCreateInfo;
    queueCreateInfo.queueFamilyIndex = m_graphicsQueueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;

    std::vector<const char*> deviceExtensionNames = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

    vk::PhysicalDeviceFeatures deviceFeatures;  // 全默认

    vk::DeviceCreateInfo deviceCreateInfo;
    deviceCreateInfo.queueCreateInfoCount = 1;
    deviceCreateInfo.pQueueCreateInfos = &queueCreateInfo;
    deviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensionNames.size());
    deviceCreateInfo.ppEnabledExtensionNames = deviceExtensionNames.data();
    deviceCreateInfo.pEnabledFeatures = &deviceFeatures;

    m_device = m_physicalDevice.createDeviceUnique(deviceCreateInfo);

    // 获取 graphics queue
    m_graphicsQueue = m_device->getQueue(m_graphicsQueueFamily, 0);

    std::cout << "[Vulkan] Device initialized.\n";
}
```

- [ ] **Step 4: 实现 Debug Messenger**

```cpp
#ifndef NDEBUG

static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData)
{
    const char* severity = "INFO";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) severity = "WARN";
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) severity = "ERROR";

    std::cerr << "[Vulkan][" << severity << "] " << pCallbackData->pMessage << "\n";
    return VK_FALSE;  // VK_FALSE 表示不中止程序
}

void VulkanRenderer::SetupDebugMessenger() {
    vk::DebugUtilsMessengerCreateInfoEXT createInfo;
    createInfo.messageSeverity =
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
    createInfo.messageType =
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
        vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
        vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
    createInfo.pfnUserCallback = DebugCallback;

    m_debugMessenger = m_instance->createDebugUtilsMessengerEXTUnique(createInfo);
}
#endif
```

- [ ] **Step 5: 编译验证**

```bash
cd E:/HeliosEngine/build && cmake --build . --target HeliosCore --config Debug 2>&1
```

Expected: 编译通过（可能有 link 错误因为 Shutdown/Render 还没实现）

---

### Task 3: Surface + SwapChain 初始化

**Files:**
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp` — 在 Initialize() 的 Device 部分后追加 Surface + SwapChain 创建

**Interfaces:**
- Consumes: `m_instance`, `m_physicalDevice`, `m_device`, `m_graphicsQueueFamily`（来自 Task 2）
- Produces: `m_surface`, `m_swapChain`, `m_swapChainFormat`, `m_swapChainExtent`, `m_swapChainImages`, `m_swapChainImageViews`

- [ ] **Step 1: 在 Initialize() Device 代码后追加 Surface 创建**

```cpp
    // ================================================================
    // 4. Surface 创建
    // ================================================================

    vk::Win32SurfaceCreateInfoKHR surfaceCreateInfo;
    surfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
    surfaceCreateInfo.hwnd = hwnd;

    m_surface = m_instance->createWin32SurfaceKHRUnique(surfaceCreateInfo);

    // ================================================================
    // 5. SwapChain 创建
    // ================================================================

    // 5a. 查询 surface capabilities
    auto surfaceCaps = m_physicalDevice.getSurfaceCapabilitiesKHR(m_surface.get());

    // 5b. 选 surface format: 优先 B8G8R8A8_UNORM + sRGB
    auto surfaceFormats = m_physicalDevice.getSurfaceFormatsKHR(m_surface.get());
    m_swapChainFormat = vk::Format::eB8G8R8A8Unorm;
    for (const auto& fmt : surfaceFormats) {
        if (fmt.format == vk::Format::eB8G8R8A8Unorm &&
            fmt.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
            m_swapChainFormat = fmt.format;
            break;
        }
    }

    // 5c. 选 present mode: FIFO = vsync（保证可用）
    auto presentModes = m_physicalDevice.getSurfacePresentModesKHR(m_surface.get());
    vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;

    // 5d. Extent: 匹配窗口大小，clamp 到 capabilities
    m_swapChainExtent = surfaceCaps.currentExtent;
    if (m_swapChainExtent.width == UINT32_MAX) {
        // 有些驱动用 UINT32_MAX 表示"未指定"
        m_swapChainExtent.width = std::clamp(static_cast<uint32_t>(width),
            surfaceCaps.minImageExtent.width,
            surfaceCaps.maxImageExtent.width);
        m_swapChainExtent.height = std::clamp(static_cast<uint32_t>(height),
            surfaceCaps.minImageExtent.height,
            surfaceCaps.maxImageExtent.height);
    }

    // 5e. Image count: min + 1 避免 acquire 时等待
    uint32_t imageCount = surfaceCaps.minImageCount + 1;
    if (surfaceCaps.maxImageCount > 0 && imageCount > surfaceCaps.maxImageCount) {
        imageCount = surfaceCaps.maxImageCount;
    }

    vk::SwapchainCreateInfoKHR swapChainCreateInfo;
    swapChainCreateInfo.surface = m_surface.get();
    swapChainCreateInfo.minImageCount = imageCount;
    swapChainCreateInfo.imageFormat = m_swapChainFormat;
    swapChainCreateInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
    swapChainCreateInfo.imageExtent = m_swapChainExtent;
    swapChainCreateInfo.imageArrayLayers = 1;
    swapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
    swapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
    swapChainCreateInfo.preTransform = surfaceCaps.currentTransform;
    swapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
    swapChainCreateInfo.presentMode = presentMode;
    swapChainCreateInfo.clipped = VK_TRUE;
    swapChainCreateInfo.oldSwapchain = nullptr;

    m_swapChain = m_device->createSwapchainKHRUnique(swapChainCreateInfo);

    // 5f. 获取 swapchain images
    m_swapChainImages = m_device->getSwapchainImagesKHR(m_swapChain.get());

    // 5g. 创建 image views
    m_swapChainImageViews.reserve(m_swapChainImages.size());
    for (const auto& image : m_swapChainImages) {
        vk::ImageViewCreateInfo viewInfo;
        viewInfo.image = image;
        viewInfo.viewType = vk::ImageViewType::e2D;
        viewInfo.format = m_swapChainFormat;
        viewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;

        m_swapChainImageViews.push_back(
            m_device->createImageViewUnique(viewInfo));
    }

    std::cout << "[Vulkan] SwapChain created: "
              << m_swapChainExtent.width << "x" << m_swapChainExtent.height
              << ", " << imageCount << " images\n";
```

- [ ] **Step 2: 编译验证**

```bash
cd E:/HeliosEngine/build && cmake --build . --target HeliosCore --config Debug 2>&1
```

Expected: 编译通过

---

### Task 4: RenderPass + GraphicsPipeline 创建

**Files:**
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp` — 在 Initialize() 的 SwapChain 代码后追加 Pipeline 创建

**Interfaces:**
- Consumes: `m_device`, `m_swapChainFormat`, `m_swapChainExtent`, `m_swapChainImageViews`
- Produces: `m_renderPass`, `m_pipelineLayout`, `m_pipeline`, `m_framebuffers`

- [ ] **Step 1: 追加 RenderPass 创建代码**

```cpp
    // ================================================================
    // 6. RenderPass 创建
    // ================================================================

    // 6a. Color attachment description
    vk::AttachmentDescription colorAttachment;
    colorAttachment.format = m_swapChainFormat;
    colorAttachment.samples = vk::SampleCountFlagBits::e1;
    colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;   // 开始时清屏
    colorAttachment.storeOp = vk::AttachmentStoreOp::eStore; // 结束时保存
    colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
    colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
    colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

    // 6b. Subpass 引用 color attachment
    vk::AttachmentReference colorAttachmentRef;
    colorAttachmentRef.attachment = 0;  // layout(location=0) 对应
    colorAttachmentRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    // 6c. Subpass dependency（等 swapchain image 就绪）
    vk::SubpassDependency dependency;
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.srcAccessMask = vk::AccessFlagBits::eNone;
    dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;

    m_renderPass = m_device->createRenderPassUnique(renderPassInfo);
```

- [ ] **Step 2: 追加 Pipeline 创建代码**

```cpp
    // ================================================================
    // 7. GraphicsPipeline 创建
    // ================================================================

    // 7a. Shader modules
    auto vertModule = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo{}
            .setCodeSize(sizeof(kVertSPIRV))
            .setPCode(kVertSPIRV));

    auto fragModule = m_device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo{}
            .setCodeSize(sizeof(kFragSPIRV))
            .setPCode(kFragSPIRV));

    // 7b. Shader stages
    vk::PipelineShaderStageCreateInfo vertStage;
    vertStage.stage = vk::ShaderStageFlagBits::eVertex;
    vertStage.module = vertModule.get();
    vertStage.pName = "main";

    vk::PipelineShaderStageCreateInfo fragStage;
    fragStage.stage = vk::ShaderStageFlagBits::eFragment;
    fragStage.module = fragModule.get();
    fragStage.pName = "main";

    std::vector<vk::PipelineShaderStageCreateInfo> shaderStages = { vertStage, fragStage };

    // 7c. Vertex input（三角形无 vertex buffer，位置在 shader 里硬编码）
    vk::PipelineVertexInputStateCreateInfo vertexInput;
    vertexInput.vertexBindingDescriptionCount = 0;
    vertexInput.vertexAttributeDescriptionCount = 0;

    // 7d. Input assembly
    vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    // 7e. Viewport + Scissor（动态状态）
    vk::PipelineViewportStateCreateInfo viewportState;
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    // 7f. Rasterizer
    vk::PipelineRasterizationStateCreateInfo rasterizer;
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;  // 三角形不裁剪，简单起见
    rasterizer.frontFace = vk::FrontFace::eClockwise;

    // 7g. Multisampling
    vk::PipelineMultisampleStateCreateInfo multisampling;
    multisampling.sampleShadingEnable = VK_FALSE;
    multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

    // 7h. Color blending: 简单叠加，无 alpha blending
    vk::PipelineColorBlendAttachmentState colorBlendAttachment;
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
    colorBlendAttachment.blendEnable = VK_FALSE;

    vk::PipelineColorBlendStateCreateInfo colorBlending;
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    // 7i. Dynamic states: viewport + scissor 运行时设置
    std::vector<vk::DynamicState> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    };
    vk::PipelineDynamicStateCreateInfo dynamicState;
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    // 7j. PipelineLayout（空——Hello Triangle 不需要 descriptor）
    vk::PipelineLayoutCreateInfo pipelineLayoutInfo;
    pipelineLayoutInfo.setLayoutCount = 0;
    pipelineLayoutInfo.pushConstantRangeCount = 0;

    m_pipelineLayout = m_device->createPipelineLayoutUnique(pipelineLayoutInfo);

    // 7k. Graphics pipeline
    vk::GraphicsPipelineCreateInfo pipelineInfo;
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = m_pipelineLayout.get();
    pipelineInfo.renderPass = m_renderPass.get();
    pipelineInfo.subpass = 0;

    auto pipelineResult = m_device->createGraphicsPipelineUnique(nullptr, pipelineInfo);
    if (pipelineResult.result != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to create graphics pipeline!");
    }
    m_pipeline = std::move(pipelineResult.value);

    // ================================================================
    // 8. Framebuffers 创建（每个 swapchain image 一个）
    // ================================================================

    m_framebuffers.reserve(m_swapChainImageViews.size());
    for (const auto& imageView : m_swapChainImageViews) {
        vk::FramebufferCreateInfo framebufferInfo;
        framebufferInfo.renderPass = m_renderPass.get();
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = &imageView.get();
        framebufferInfo.width = m_swapChainExtent.width;
        framebufferInfo.height = m_swapChainExtent.height;
        framebufferInfo.layers = 1;

        m_framebuffers.push_back(
            m_device->createFramebufferUnique(framebufferInfo));
    }

    std::cout << "[Vulkan] Pipeline + Framebuffers created.\n";
```

- [ ] **Step 2: 编译验证**

```bash
cd E:/HeliosEngine/build && cmake --build . --target HeliosCore --config Debug 2>&1
```

Expected: 编译通过

---

### Task 5: CommandBuffer + 录制

**Files:**
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp` — 在 Initialize() 的 Pipeline 代码后追加 CommandBuffer 创建 + 录制逻辑

**Interfaces:**
- Consumes: `m_device`, `m_graphicsQueueFamily`, `m_renderPass`, `m_pipeline`, `m_pipelineLayout`, `m_framebuffers`, `m_swapChainExtent`
- Produces: `m_commandPool`, `m_commandBuffer` (with recorded commands)

- [ ] **Step 1: 追加 CommandPool + CommandBuffer 创建和录制**

```cpp
    // ================================================================
    // 9. CommandPool + CommandBuffer
    // ================================================================

    vk::CommandPoolCreateInfo poolInfo;
    poolInfo.queueFamilyIndex = m_graphicsQueueFamily;
    poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

    m_commandPool = m_device->createCommandPoolUnique(poolInfo);

    vk::CommandBufferAllocateInfo allocInfo;
    allocInfo.commandPool = m_commandPool.get();
    allocInfo.level = vk::CommandBufferLevel::ePrimary;
    allocInfo.commandBufferCount = 1;

    auto cmdBuffers = m_device->allocateCommandBuffersUnique(allocInfo);
    // … 但 allocateCommandBuffersUnique 返回 UniqueCommandBuffer 列表
    // 或者用非 unique 版本：
    auto cmdBufResult = m_device->allocateCommandBuffers(allocInfo);
    if (cmdBufResult.size() > 0) {
        m_commandBuffer = cmdBufResult[0];
    }

    // ================================================================
    // 10. Sync objects
    // ================================================================

    m_imageAvailableSemaphore = m_device->createSemaphoreUnique({});
    m_renderFinishedSemaphore = m_device->createSemaphoreUnique({});

    std::cout << "[Vulkan] CommandBuffer + Sync objects created.\n";
}
```

> ⚠️ **注意**：`allocateCommandBuffersUnique` 的行为需要确认。如果 Vulkan-HPP 版本不返回 Unique，就用非 unique 的 `allocateCommandBuffers`。CommandBuffer 被 pool 管理生命周期，不需要单独销毁。

- [ ] **Step 2: 实现 RecordCommandBuffer 辅助方法**

在 `VulkanRenderer.h` 私有部分添加声明：

```cpp
    void RecordCommandBuffer(uint32_t imageIndex);
```

在 .cpp 中实现：

```cpp
void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex) {
    // 重置并开始录制
    m_commandBuffer.reset();

    vk::CommandBufferBeginInfo beginInfo;
    beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

    m_commandBuffer.begin(beginInfo);

    // --- RenderPass ---
    vk::ClearValue clearColor;
    clearColor.color = vk::ClearColorValue(std::array<float, 4>{0.02f, 0.02f, 0.06f, 1.0f});

    vk::RenderPassBeginInfo renderPassInfo;
    renderPassInfo.renderPass = m_renderPass.get();
    renderPassInfo.framebuffer = m_framebuffers[imageIndex].get();
    renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
    renderPassInfo.renderArea.extent = m_swapChainExtent;
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    m_commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

    // --- Bind pipeline ---
    m_commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_pipeline.get());

    // --- Set dynamic state ---
    vk::Viewport viewport;
    viewport.x = 0.0f;
    viewport.y = 0.0f;
    viewport.width = static_cast<float>(m_swapChainExtent.width);
    viewport.height = static_cast<float>(m_swapChainExtent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;
    m_commandBuffer.setViewport(0, 1, &viewport);

    vk::Rect2D scissor;
    scissor.offset = vk::Offset2D{0, 0};
    scissor.extent = m_swapChainExtent;
    m_commandBuffer.setScissor(0, 1, &scissor);

    // --- Draw triangle (3 vertices, 1 instance) ---
    m_commandBuffer.draw(3, 1, 0, 0);

    // --- End ---
    m_commandBuffer.endRenderPass();
    m_commandBuffer.end();
}
```

- [ ] **Step 3: 编译验证**

```bash
cd E:/HeliosEngine/build && cmake --build . --target HeliosCore --config Debug 2>&1
```

---

### Task 6: Render 循环 + Shutdown

**Files:**
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp` — 实现 Render() 和 Shutdown()

**Interfaces:**
- Consumes: 所有已创建的 Vulkan 对象
- Produces: `Render()`（每帧调用）和 `Shutdown()`（退出时逆序销毁）

- [ ] **Step 1: 实现 Render()**

```cpp
void VulkanRenderer::Render() {
    // 1. Acquire next image
    uint32_t imageIndex;
    vk::Result acquireResult = m_device->acquireNextImageKHR(
        m_swapChain.get(),
        UINT64_MAX,                       // timeout
        m_imageAvailableSemaphore.get(),  // signal when ready
        nullptr,                          // no fence
        &imageIndex);

    if (acquireResult == vk::Result::eErrorOutOfDateKHR) {
        // 窗口 resize——先跳过，Phase 2 处理
        std::cerr << "[Vulkan] SwapChain out of date (window resized?)\n";
        return;
    } else if (acquireResult != vk::Result::eSuccess &&
               acquireResult != vk::Result::eSuboptimalKHR) {
        throw std::runtime_error("Failed to acquire swapchain image!");
    }

    // 2. Reset and record command buffer
    RecordCommandBuffer(imageIndex);

    // 3. Submit
    vk::PipelineStageFlags waitStages[] = {
        vk::PipelineStageFlagBits::eColorAttachmentOutput
    };
    vk::SubmitInfo submitInfo;
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = &m_imageAvailableSemaphore.get();
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &m_commandBuffer;
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = &m_renderFinishedSemaphore.get();

    m_graphicsQueue.submit(submitInfo, nullptr);

    // 4. Present
    vk::PresentInfoKHR presentInfo;
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = &m_renderFinishedSemaphore.get();
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = &m_swapChain.get();
    presentInfo.pImageIndices = &imageIndex;

    vk::Result presentResult = m_graphicsQueue.presentKHR(&presentInfo);

    if (presentResult == vk::Result::eErrorOutOfDateKHR ||
        presentResult == vk::Result::eSuboptimalKHR) {
        // 窗口 resize——跳过
    } else if (presentResult != vk::Result::eSuccess) {
        throw std::runtime_error("Failed to present!");
    }

    // 5. 等待 GPU 完成——简单同步，后续 Phase 升级为 Fence + 多帧
    m_device->waitIdle();
}
```

- [ ] **Step 2: 实现 Shutdown()**

```cpp
void VulkanRenderer::Shutdown() {
    // 等待 GPU 完成所有工作后再销毁资源
    if (m_device) {
        m_device->waitIdle();
    }

    // 显式重置对象，按依赖逆序销毁。
    // vk::UniqueHandle 会自动释放，但显式调用 Shutdown 可以控制顺序。
    // 实际上 ~UniqueHandle 的调用顺序由 C++ 析构规则保证（成员声明逆序），
    // 所以我们只需要确保 device 还活着时手动 reset 需要它的资源。

    m_imageAvailableSemaphore.reset();
    m_renderFinishedSemaphore.reset();

    m_commandPool.reset();  // 会隐式释放 m_commandBuffer

    m_framebuffers.clear();
    m_pipeline.reset();
    m_pipelineLayout.reset();
    m_renderPass.reset();

    m_swapChainImageViews.clear();
    // m_swapChainImages 由 swapchain 拥有，不需要单独释放
    m_swapChain.reset();
    m_surface.reset();

#ifndef NDEBUG
    m_debugMessenger.reset();
#endif

    m_device.reset();
    m_instance.reset();

    std::cout << "[Vulkan] Shutdown complete.\n";
}
```

> ⚠️ 实际上 vk::UniqueHandle 成员变量在析构时自动逆序释放，`Shutdown()` 的显式 reset 是可选的。但显式写出来有助于理解销毁顺序，也方便调试时打断点。

- [ ] **Step 3: 添加 ~VulkanRenderer() 析构**

在 `VulkanRenderer.h` 头文件中添加（或依赖成员自动析构）：

```cpp
public:
    ~VulkanRenderer() = default;  // UniqueHandle 自动清理
```

- [ ] **Step 4: 编译验证**

```bash
cd E:/HeliosEngine/build && cmake --build . --target HeliosCore --config Debug 2>&1
```

---

### Task 7: 集成到 main.cpp

**Files:**
- Modify: `src/main.cpp` — 创建 VulkanRenderer 并调用 Render()

- [ ] **Step 1: 更新 main.cpp**

```cpp
#include "core/Platform/Window.h"
#include "core/Render/Vulkan/VulkanRenderer.h"

#include <windows.h>
#include <iostream>

int WINAPI WinMain(HINSTANCE hInstance,
                   HINSTANCE hPrevInstance,
                   LPSTR lpCmdLine,
                   int nCmdShow)
{
    // Phase 1.1: Win32 窗口
    Helios::Window window(L"HeliosEngine — Vulkan Hello Triangle", 1280, 720);

    std::cout << "Window created. HWND: " << window.GetHwnd() << "\n";

    // Phase 1.2: Vulkan 渲染器
    Helios::VulkanRenderer renderer;

    try {
        renderer.Initialize(window.GetHwnd(), window.GetWidth(), window.GetHeight());

        // 游戏循环
        while (window.ProcessMessages()) {
            renderer.Render();
        }
    } catch (const std::exception& e) {
        std::cerr << "[FATAL] " << e.what() << "\n";
        MessageBoxA(nullptr, e.what(), "Vulkan Error", MB_ICONERROR | MB_OK);
        return 1;
    }

    // 先关闭 renderer（需要 device 活着来销毁资源），再关闭窗口
    renderer.Shutdown();

    std::cout << "Clean exit.\n";
    return 0;
}
```

- [ ] **Step 2: 编译**

```bash
cd E:/HeliosEngine/build && cmake --build . --config Debug 2>&1
```

Expected: 编译 + 链接成功

---

### Task 8: 构建、运行、验证

- [ ] **Step 1: Debug 构建运行**

```bash
cd E:/HeliosEngine/build && cmake --build . --config Debug 2>&1 && ./bin/Debug/HeliosEngine.exe
```

Expected:
- 窗口弹出，标题 "HeliosEngine — Vulkan Hello Triangle"
- 窗口背景深蓝黑色，中间显示蓝色三角形
- 控制台输出 Vulkan 初始化信息
- 无 validation layer error/warning
- 关闭窗口后输出 "Clean exit."

- [ ] **Step 2: Release 构建运行**

```bash
cd E:/HeliosEngine/build && cmake --build . --config Release 2>&1 && ./bin/Release/HeliosEngine.exe
```

Expected: 同上，但无 validation layer 信息

- [ ] **Step 3: 关闭后检查 validation report**

Debug 构建正常退出时，控制台不应有 "ERROR" 级别的 debug message。如果有，修正。

---

### Task 9: Commit

**Files:**
- 所有修改的文件

```bash
git add .
git commit -m "feat: Vulkan Hello Triangle — Phase 1.2
- 添加 VulkanRenderer 类，封装完整 Vulkan 初始化流程
- Instance → Device → SwapChain → Pipeline → CommandBuffer
- 硬编码 SPIR-V shader（三角形顶点 + 深蓝像素）
- Debug 构建启用 validation layer + debug messenger
- 简单同步：每帧 waitIdle
- 后续重构目标见 docs/superpowers/specs/2026-07-11-vulkan-hello-triangle-design.md

Co-Authored-By: Claude <noreply@anthropic.com>"
```
