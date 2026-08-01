#pragma once

// windows.h must come before vulkan.hpp — the Win32 surface creation needs HWND.
// VK_USE_PLATFORM_WIN32_KHR: tells vulkan.hpp we target a Win32 window.
// VK_NO_PROTOTYPES: don't link vulkan-1.lib statically; load entry points from vulkan-1.dll at runtime instead.
//   -> extension functions (debug utils, etc.) are also fetched dynamically, no manual vkGetInstanceProcAddr needed.
// VULKAN_HPP_DISPATCH_LOADER_DYNAMIC: enables vk::DynamicLoader machinery.
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
// VulkanRenderer — owns every Vulkan object and the draw flow.
//
// Design intent: get the whole Hello Triangle flow working inside one class,
// then later split it into VulkanDevice / VulkanSwapChain / VulkanPipeline, etc.
//
// Object lifetime: member declaration order = reverse of destruction order.
// C++ destroys members in reverse-declaration order -> guarantees Device is
// destroyed before Instance, SwapChain before Device, and so on, with no
// manual dependency management.
//
// This build renders 5 triangles (multi-object, per-object MVP via a dynamic-offset
// uniform buffer) into an OFFSCREEN intermediate image (SceneColor) in subpass 0, then in subpass 1
// reads SceneColor as an input attachment and runs a post shader (invert)
// into the SwapChain. That demonstrates a base-pass -> post-process-pass
// chained through a subpass dependency.
//
// Init order (10 steps, must not be reordered):
//   1.Instance -> 2.PhysicalDevice -> 3.Device+Queue -> 4.Surface -> 5.SwapChain
//   -> 6.RenderPass -> 7.Pipeline -> 8.Framebuffers -> 9.CommandPool+Buffer -> 10.Sync
//
// Per-frame flow (5 steps):
//   1.acquireNextImage -> 2.record -> 3.submit -> 4.present -> 5.waitIdle
//   Currently uses waitIdle for simple sync; a later phase swaps in Fences
//   to overlap multiple frames.
// =========================================================================
class VulkanRenderer
{
  public:
	// Initialize every Vulkan object. InHwnd comes from Window::GetHwnd().
	void Initialize(HWND InHwnd, int InWidth, int InHeight);

	// Render one frame. Called once per frame from the game loop.
	void Render();

	// Explicitly destroy all resources in reverse order. Relying on the
	// destructor (members are UniqueHandles) also works, but calling this
	// ensures dependents are freed while the device is still alive.
	void Shutdown();

  private:
	void SetupDebugMessenger();
	void RecordCommandBuffer(uint32_t ImageIndex);
	uint32_t FindMemoryType(uint32_t TypeFilter, vk::MemoryPropertyFlags Properties);

	// ---- Instance & Device ----
	// Instance: the Vulkan entry point, root of all Vulkan objects.
	// PhysicalDevice: abstract GPU — query capabilities, pick a card.
	// Device: the "connection" to the GPU — create everything else, submit commands.
	// GraphicsQueueFamily: index of the GPU's graphics queue family.
	// GraphicsQueue: the actual queue commands are submitted to.
	vk::UniqueInstance m_Instance;
	vk::PhysicalDevice m_PhysicalDevice = nullptr;
	vk::UniqueDevice m_Device;
	uint32_t m_GraphicsQueueFamily = 0;
	vk::Queue m_GraphicsQueue = nullptr;
	vk::UniqueDebugUtilsMessengerEXT m_DebugMessenger;

	// ---- Surface & SwapChain ----
	// Surface: bridge between Vulkan and the Win32 window.
	// SwapChain: a set of Images cycled for drawing — one shown while another is rendered.
	// SwapChainImages: Images owned by the SwapChain, not freed individually.
	// SwapChainImageViews: one View per Image; the RenderPass writes into the Image via the View.
	vk::UniqueSurfaceKHR m_Surface;
	vk::UniqueSwapchainKHR m_SwapChain;
	vk::Format m_SwapChainFormat = vk::Format::eUndefined; // B8G8R8A8_UNORM
	vk::Extent2D m_SwapChainExtent = {0, 0};			   // window size in pixels
	std::vector<vk::Image> m_SwapChainImages;
	std::vector<vk::UniqueImageView> m_SwapChainImageViews;

	// ---- Pipeline ----
	// RenderPass: describes the 2-subpass flow (offscreen write -> input-attachment read).
	// PipelineLayout: binds the descriptor set layout; the vertex shader reads each
	// object's MVP from a dynamic-offset uniform buffer (binding 0).
	// Pipeline: packs the triangle Shader + fixed state into one immutable object (like a DX12 PSO).
	// Framebuffers: one per SwapChain Image — binds RenderPass + the 2 attachments together.
	vk::UniqueRenderPass m_RenderPass;
	vk::UniquePipelineLayout m_PipelineLayout;
	vk::UniquePipeline m_Pipeline;
	std::vector<vk::UniqueFramebuffer> m_Framebuffers;

	// ---- Vertex buffer ----
	// The base triangle's geometry now lives in GPU memory instead of hardcoded
	// arrays in the shader. Memory is declared before the buffer so the buffer is
	// destroyed first (reverse-declaration order), then its backing memory freed.
	vk::UniqueDeviceMemory m_VertexBufferMemory;
	vk::UniqueBuffer m_VertexBuffer;

	// ---- Per-object MVP uniform buffer + descriptor ----
	// One host-visible UBO holds 5 combined MVP matrices; the GPU picks a slot per
	// draw via a dynamic offset. Frequency-layering counterpart to the vertex buffer:
	// geometry is static (device-local), the per-object matrices change every frame
	// (host-visible, rewritten in place). Memory declared before buffer: buffer frees
	// before its backing memory.
	vk::UniqueDeviceMemory m_UniformBufferMemory;
	vk::UniqueBuffer m_UniformBuffer;
	void* m_UniformBufferMapped = nullptr;
	uint32_t m_UniformBufferStride = 0;
	vk::UniqueDescriptorSetLayout m_DescriptorSetLayout;
	vk::UniqueDescriptorPool m_DescriptorPool;
	vk::UniqueDescriptorSet m_DescriptorSet;

	// ---- Offscreen intermediate (SceneColor) + post subpass pipeline ----
	// SceneColor: an offscreen image. subpass 0 writes the 5 triangles into it;
	// subpass 1 reads it as an input attachment. Declared before its memory so
	// the UniqueHandle destructor frees the image before the memory (reverse order).
	vk::UniqueDeviceMemory m_SceneColorMemory;
	vk::UniqueImage m_SceneColorImage;
	vk::UniqueImageView m_SceneColorView;

	// Post: subpass 1's pipeline + descriptor. An input attachment must be bound
	// through a descriptor set, so we need a layout, pool and set for it.
	vk::UniqueDescriptorSetLayout m_PostDescriptorSetLayout;
	vk::UniqueDescriptorPool m_PostDescriptorPool;
	vk::UniqueDescriptorSet m_PostDescriptorSet;
	vk::UniquePipelineLayout m_PostPipelineLayout;
	vk::UniquePipeline m_PostPipeline;

	// ---- Command ----
	// CommandPool: memory pool for CommandBuffers. One Pool can allocate many.
	// CommandBuffer: records GPU commands (bind pipeline, draw, clear...), then is submitted to a Queue.
	vk::UniqueCommandPool m_CommandPool;
	vk::CommandBuffer m_CommandBuffer = nullptr;

	// ---- Sync ----
	// Semaphore: GPU-GPU sync primitive. One signals "image acquired", one "render finished".
	vk::UniqueSemaphore m_ImageAvailableSemaphore;
	vk::UniqueSemaphore m_RenderFinishedSemaphore;
};

} // namespace Helios
