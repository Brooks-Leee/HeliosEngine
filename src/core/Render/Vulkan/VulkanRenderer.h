#pragma once

// windows.h 必须在 vulkan.hpp 之前——Win32 surface 创建需要 HWND 类型。
// VK_USE_PLATFORM_WIN32_KHR：告诉 vulkan.hpp 我们要用 Win32 窗口。
// VK_NO_PROTOTYPES：不链接 vulkan-1.lib 的静态函数，改为运行时从 vulkan-1.dll 动态加载。
//   → extension 函数（debug utils 等）也通过动态加载获取，不需要手动 vkGetInstanceProcAddr。
// VULKAN_HPP_DISPATCH_LOADER_DYNAMIC：启用 vk::DynamicLoader 相关功能。
#include <windows.h>
#define VK_USE_PLATFORM_WIN32_KHR
#define VK_NO_PROTOTYPES
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <vector>

namespace Helios
{

// =========================================================================
// VulkanRenderer — 集中管理所有 Vulkan 对象和绘制流程
//
// 设计意图：先把整个 Hello Triangle 流程写在一个类里，跑通后再拆分为
// VulkanDevice / VulkanSwapChain / VulkanPipeline 等细粒度模块。
//
// 对象生命周期：成员声明顺序 = 初始化顺序的逆序。
// C++ 析构顺序是声明逆序 → 自动保证 Device 在 Instance 之前销毁、
// SwapChain 在 Device 之前销毁……不需要手动管理依赖。
//
// 初始化顺序（10 步，顺序不可乱）：
//   1.Instance → 2.PhysicalDevice → 3.Device+Queue → 4.Surface → 5.SwapChain
//   → 6.RenderPass → 7.Pipeline → 8.Framebuffers → 9.CommandPool+Buffer → 10.Sync
//
// 每帧流程（5 步）：
//   1.acquireNextImage → 2.record → 3.submit → 4.present → 5.waitIdle
//   当前用 waitIdle 做简单同步，后续 Phase 替换为 Fence 实现多帧并行。
// =========================================================================
class VulkanRenderer
{
  public:
	// 初始化全部 Vulkan 对象。InHwnd 来自 Window::GetHwnd()。
	void Initialize(HWND InHwnd, int InWidth, int InHeight);

	// 渲染一帧。每帧调用一次，放在游戏循环里。
	void Render();

	// 显式逆序销毁所有资源。也可以依赖析构（成员 UniqueHandle 自动释放），
	// 但显式调用能保证 device 活着时销毁依赖它的资源。
	void Shutdown();

  private:
	void SetupDebugMessenger();
	void RecordCommandBuffer(uint32_t ImageIndex);

	// ---- Instance & Device ----
	// Instance：Vulkan 的入口，所有 Vulkan 对象的"根"。
	// PhysicalDevice：GPU 的抽象——查询能力、选显卡。
	// Device：和 GPU 的"连接"——从这里创建所有其他对象、提交命令。
	// GraphicsQueueFamily：GPU 上 graphics 队列的索引号。
	// GraphicsQueue：真正往 GPU 提交命令的队列。
	vk::UniqueInstance m_Instance;
	vk::PhysicalDevice m_PhysicalDevice = nullptr;
	vk::UniqueDevice m_Device;
	uint32_t m_GraphicsQueueFamily = 0;
	vk::Queue m_GraphicsQueue = nullptr;
	vk::UniqueDebugUtilsMessengerEXT m_DebugMessenger;

	// ---- Surface & SwapChain ----
	// Surface：Vulkan 和 Win32 窗口的"桥梁"。
	// SwapChain：一组可以轮流绘制的 Image——一个在显示时，另一个在渲染。
	// SwapChainImages：SwapChain 拥有的 Image，不单独释放。
	// SwapChainImageViews：每个 Image 对应一个 View，RenderPass 通过 View 写入 Image。
	vk::UniqueSurfaceKHR m_Surface;
	vk::UniqueSwapchainKHR m_SwapChain;
	vk::Format m_SwapChainFormat = vk::Format::eUndefined; // B8G8R8A8_UNORM
	vk::Extent2D m_SwapChainExtent = {0, 0};			   // 窗口像素尺寸
	std::vector<vk::Image> m_SwapChainImages;
	std::vector<vk::UniqueImageView> m_SwapChainImageViews;

	// ---- Pipeline ----
	// RenderPass：描述"渲染到哪个格式的 Image、怎么处理 Clear/Store"。
	// PipelineLayout：Pipeline 使用哪些资源（DescriptorSet + PushConstant）。Hello Triangle 为空。
	// Pipeline：将 Shader + 混合/深度/光栅化等固定状态打包成一个不可变对象（类似 DX12 PSO）。
	// Framebuffers：每个 SwapChain Image 一个——RenderPass + ImageView 的绑定。
	vk::UniqueRenderPass m_RenderPass;
	vk::UniquePipelineLayout m_PipelineLayout;
	vk::UniquePipeline m_Pipeline;
	std::vector<vk::UniqueFramebuffer> m_Framebuffers;

	// ---- Command ----
	// CommandPool：CommandBuffer 的内存池。一个 Pool 可以分配多个 Buffer。
	// CommandBuffer：录制 GPU 命令（bind pipeline、draw、clear 等），录完后提交到 Queue。
	vk::UniqueCommandPool m_CommandPool;
	vk::CommandBuffer m_CommandBuffer = nullptr;

	// ---- Sync ----
	// Semaphore：GPU-GPU 同步原语。一个信号"image 拿到了"，一个信号"渲染完成了"。
	vk::UniqueSemaphore m_ImageAvailableSemaphore;
	vk::UniqueSemaphore m_RenderFinishedSemaphore;
};

} // namespace Helios
