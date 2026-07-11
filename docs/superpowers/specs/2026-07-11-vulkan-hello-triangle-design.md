# Vulkan Hello Triangle — 设计文档

> Phase 1.2 | 2026-07-11

## 设计决策

- **Vulkan 版本**：API version `VK_API_VERSION_1_4`（SDK 1.4.350.0）。渲染路径走传统 RenderPass + Framebuffer，不使用 dynamic rendering（`VK_KHR_dynamic_rendering`）。1.4 向后兼容 1.0 的所有 API。
- **Vulkan 优先**：先深入 Vulkan 的具体实现，不做 RHI 抽象。等 DX12 也写完后（Phase 1.3），基于两个 API 的第一手经验再提炼公共接口。
- **C++ bindings (`vulkan.hpp`)**：RAII (`vk::UniqueHandle`)，类型安全，与项目现有 RAII 哲学一致。
- **异常处理**：使用 `vk::resultCheck()` 和 Vulkan-HPP 原生的异常机制，代码简洁，调试时堆栈信息完整。

## 文件结构

```
src/core/Render/Vulkan/
├── VulkanDevice.h/.cpp          # Instance → PhysicalDevice → LogicalDevice
├── VulkanSwapChain.h/.cpp       # Surface + SwapChain + ImageViews
├── VulkanPipeline.h/.cpp        # RenderPass + PipelineLayout + GraphicsPipeline
├── VulkanCommandBuffer.h/.cpp   # CommandPool + 录制 draw commands
├── VulkanRenderer.h/.cpp        # 总调度：初始化所有组件，编排每帧渲染
```

每个文件对应 roadmap 1.3 的一个子项，职责单一，可独立测试。

## 组件设计

### VulkanDevice

**职责**：创建 Vulkan 实例和设备，选物理设备。

- `vk::Instance`：application info + validation layers（Debug 构建开启 `VK_LAYER_KHRONOS_validation`）
- 遍历 `enumeratePhysicalDevices()`，选独立 GPU（`VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU`），fallback 到集成显卡
- 查找 graphics queue family，创建 `vk::Device`，启用 swapchain extension
- Debug 构建注册 `VK_EXT_debug_utils` messenger，打印 validation 信息

```cpp
class VulkanDevice {
public:
    void Initialize();  // 创建 instance → physical device → logical device
    void Shutdown();    // 逆序销毁

    vk::Instance        GetInstance() const;
    vk::PhysicalDevice  GetPhysicalDevice() const;
    vk::Device          GetDevice() const;
    uint32_t            GetGraphicsQueueFamily() const;
    vk::Queue           GetGraphicsQueue() const;

private:
    vk::UniqueInstance              m_instance;
    vk::PhysicalDevice              m_physicalDevice;
    vk::UniqueDevice                m_device;
    uint32_t                        m_graphicsQueueFamily = 0;
    vk::Queue                       m_graphicsQueue;
    vk::UniqueDebugUtilsMessengerEXT m_debugMessenger;  // Debug only
};
```

### VulkanSwapChain

**职责**：管理 Surface 和 SwapChain，提供可渲染的 image views。

- 从 HWND 创建 `vk::SurfaceKHR`（`vk::Win32SurfaceCreateInfoKHR`）
- 查询 surface capabilities → 选 format（`VK_FORMAT_B8G8R8A8_UNORM`）
- Present mode：`VK_PRESENT_MODE_FIFO_KHR`（vsync，保证有）
- Extent：匹配窗口客户区尺寸（与 capabilities 的 min/max clamp）
- `minImageCount + 1` 避免 acquire 时等待
- 创建 image views

```cpp
class VulkanSwapChain {
public:
    void Initialize(const VulkanDevice& device, HWND hwnd, int width, int height);
    void Shutdown();

    vk::Format        GetFormat() const;
    vk::Extent2D      GetExtent() const;
    uint32_t          GetImageCount() const;
    vk::ImageView     GetImageView(uint32_t index) const;
    vk::SwapchainKHR  GetSwapChain() const;

private:
    vk::UniqueSurfaceKHR    m_surface;
    vk::UniqueSwapchainKHR  m_swapChain;
    vk::Format              m_format;
    vk::Extent2D            m_extent;
    std::vector<vk::Image>          m_images;       // swapchain 拥有，不需要 Unique
    std::vector<vk::UniqueImageView> m_imageViews;
};
```

### VulkanPipeline

**职责**：创建 RenderPass、Shader Module、PipelineLayout、GraphicsPipeline、Framebuffer。

- **RenderPass**：1 个 subpass，1 个 color attachment（loadOp=Clear, storeOp=Store）
- **Shader**：硬编码 SPIR-V 字节数组（三角形 VS + FS），先不引入 shader 编译工具链
- **PipelineLayout**：空（Hello Triangle 不用 descriptor）
- **GraphicsPipeline**：无顶点输入（VS 里硬编码三个顶点位置），triangle list

```cpp
class VulkanPipeline {
public:
    void Initialize(const VulkanDevice& device, const VulkanSwapChain& swapChain);
    void Shutdown();

    vk::RenderPass     GetRenderPass() const;
    vk::Pipeline       GetPipeline() const;
    vk::PipelineLayout GetPipelineLayout() const;
    vk::Framebuffer    GetFramebuffer(uint32_t imageIndex) const;

private:
    vk::UniqueRenderPass                m_renderPass;
    vk::UniquePipelineLayout             m_pipelineLayout;
    vk::UniquePipeline                   m_pipeline;
    std::vector<vk::UniqueFramebuffer>   m_framebuffers;
};
```

### VulkanCommandBuffer

**职责**：管理 CommandPool，录制渲染命令。

- `VK_COMMAND_POOL_CREATE_TRANSIENT_BIT`：每帧重新录制
- 单 command buffer（三角形简单，不需要多 buffer）

```cpp
class VulkanCommandBuffer {
public:
    void Initialize(const VulkanDevice& device);
    void Shutdown();

    // 录制一帧的绘制命令
    void Record(const VulkanDevice& device,
                const VulkanPipeline& pipeline,
                const VulkanSwapChain& swapChain,
                uint32_t imageIndex);

    vk::CommandBuffer GetCommandBuffer() const;

private:
    vk::UniqueCommandPool   m_commandPool;
    vk::CommandBuffer       m_commandBuffer;  // 从 pool 分配，不需要 Unique
};
```

### VulkanRenderer

**职责**：总调度，组合以上组件，驱动每帧渲染。

- 初始化顺序：Device → SwapChain → Pipeline → CommandBuffer
- 每帧：acquire → record → submit → present → waitIdle（简单同步）
- Semaphore：`imageAvailable`（acquire 完成） + `renderFinished`（渲染完成）

```cpp
class VulkanRenderer {
public:
    void Initialize(HWND hwnd, int width, int height);
    void Shutdown();
    void Render();

private:
    VulkanDevice        m_device;
    VulkanSwapChain      m_swapChain;
    VulkanPipeline       m_pipeline;
    VulkanCommandBuffer  m_commandBuffer;

    vk::UniqueSemaphore  m_imageAvailableSemaphore;
    vk::UniqueSemaphore  m_renderFinishedSemaphore;
};
```

## 渲染循环流程

```
Frame N:
  1. vk::acquireNextImageKHR(m_swapChain, timeout, m_imageAvailableSemaphore, {})
     → 获得 imageIndex，GPU 信号 imageAvailable 时 CPU 可安全写入

  2. m_commandBuffer.Record(imageIndex)
     → beginRenderPass(clear color: 深蓝渐变风格)
     → bindPipeline(graphicsPipeline)
     → draw(3, 1, 0, 0)  // 三个顶点，无 vertex buffer
     → endRenderPass

  3. vk::Queue::submit(wait: imageAvailable, signal: renderFinished)
     → 提交 command buffer 到 graphics queue

  4. vk::Queue::presentKHR(wait: renderFinished, swapChain, imageIndex)
     → 显示到屏幕

  5. vk::Queue::waitIdle()
     → 等 GPU 完成，保证下一帧不覆盖正在使用的资源
     // 后续 Phase 会替换为 Fence + 多帧并行
```

## Shader 策略

**先用硬编码 SPIR-V**：将 GLSL 编译为 SPIR-V 字节数组直接嵌入 C++ 源码。

```cpp
// 硬编码三角形 shader（SPIR-V 二进制）
static const uint32_t kVertexShaderSPIRV[] = { /* glslc 编译的输出 */ };
static const uint32_t kFragmentShaderSPIRV[] = { /* glslc 编译的输出 */ };
```

原因：
- 先聚焦 Vulkan 管线和同步，不要被 shader 编译工具链分散注意力
- CMake 集成 `glslc` 是 Phase 4 的事
- 硬编码的 SPIR-V 足够驱动 Hello Triangle

对应的 GLSL 源码存放在 `shaders/vulkan/triangle.vert` 和 `shaders/vulkan/triangle.frag`，作为参考。

## CMake 修改

现有 CMake 已配置：

```cmake
if(HELIOS_BUILD_VULKAN)
    find_package(Vulkan REQUIRED)
    target_link_libraries(HeliosCore PUBLIC Vulkan::Vulkan)
endif()
```

只需在 `src/core/CMakeLists.txt` 中添加 Vulkan 源文件：

```cmake
if(HELIOS_BUILD_VULKAN)
    target_sources(HeliosCore PRIVATE
        Render/Vulkan/VulkanDevice.cpp
        Render/Vulkan/VulkanSwapChain.cpp
        Render/Vulkan/VulkanPipeline.cpp
        Render/Vulkan/VulkanCommandBuffer.cpp
        Render/Vulkan/VulkanRenderer.cpp
    )
endif()
```

## 验证标准

- [ ] Debug 构建启动 validation layer，无 error/warning
- [ ] 窗口显示深蓝背景色的三角形
- [ ] 关闭窗口时 Vulkan 资源全部正确销毁（validation layer 无 object leak 报告）
- [ ] Release 构建也能正常运行
