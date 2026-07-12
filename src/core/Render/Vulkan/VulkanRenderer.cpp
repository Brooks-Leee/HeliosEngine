#include "VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <windows.h>

// VK_NO_PROTOTYPES mode: the dispatch loader storage must be defined in exactly one translation unit.
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Helios
{

// =========================================================================
// Debug callback — prints validation layer messages to stderr
// =========================================================================
#ifndef NDEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
													VkDebugUtilsMessageTypeFlagsEXT type,
													const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
													void* /*pUserData*/)
{
	(void) type;

	const char* sevLabel = "INFO";
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
		sevLabel = "WARN";
	if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
		sevLabel = "ERROR";

	std::cerr << "[Vulkan][" << sevLabel << "] " << pCallbackData->pMessage << "\n";
	return VK_FALSE;
}
#endif

// =========================================================================
// Helper: check whether the requested validation layers are available
// =========================================================================
static bool CheckValidationLayerSupport(const std::vector<const char*>& layers)
{
	std::vector<vk::LayerProperties> available = vk::enumerateInstanceLayerProperties();
	for (auto* required : layers)
	{
		bool found = false;
		for (const auto& layer : available)
		{
			if (strcmp(layer.layerName, required) == 0)
			{
				found = true;
				break;
			}
		}
		if (!found)
			return false;
	}
	return true;
}

// =========================================================================
// Helper: read an entire .spv file into memory (binary)
// =========================================================================
static std::vector<char> ReadFile(const std::string& path)
{
	// ate = seek to end on open, so we can grab the size directly
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
	{
		throw std::runtime_error("Failed to open shader file: " + path);
	}
	size_t fileSize = static_cast<size_t>(file.tellg());
	std::vector<char> buffer(fileSize);
	file.seekg(0);
	file.read(buffer.data(), static_cast<std::streamsize>(fileSize));
	return buffer;
}

// =========================================================================
// Helper: create a ShaderModule from a .spv file (replaces the old hardcoded SPIR-V array)
// =========================================================================
static vk::UniqueShaderModule LoadShaderModule(vk::Device device, const std::string& path)
{
	std::vector<char> code = ReadFile(path);
	vk::ShaderModuleCreateInfo info;
	info.codeSize = code.size();
	// SPIR-V is a uint32 stream; a char* start is 4-byte aligned (guaranteed by vector allocation), so reinterpret is safe
	info.pCode = reinterpret_cast<const uint32_t*>(code.data());
	return device.createShaderModuleUnique(info);
}

// Push constant sent to the vertex shader per object (matches the Push block in triangle.vert).
// Layout must match the GLSL side: vec2 offset(8B) + float scale(4B) = 12B
struct TrianglePush
{
	float offset[2];
	float scale;
};

// =========================================================================
// Initialize
// =========================================================================
void VulkanRenderer::Initialize(HWND InHwnd, int InWidth, int InHeight)
{
	// =====================================================================
	// 0. Dispatch Loader
	// Unlike a normal library, Vulkan functions aren't linked at compile time. With
	// VK_NO_PROTOTYPES, every vkXxx() is resolved at runtime from vulkan-1.dll via this
	// global dispatcher. Without it nothing downstream can be called; and it must be
	// re-init (`.init`) whenever a new Instance/Device is created.
	// =====================================================================
	HMODULE vulkanDll = LoadLibraryA("vulkan-1.dll");
	if (!vulkanDll)
	{
		throw std::runtime_error("Failed to load vulkan-1.dll!");
	}
	auto vkGetInstanceProcAddr =
		reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkanDll, "vkGetInstanceProcAddr"));
	if (!vkGetInstanceProcAddr)
	{
		throw std::runtime_error("Failed to get vkGetInstanceProcAddr!");
	}
	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	// =====================================================================
	// 1. Instance — the "first handshake" with the Vulkan driver.
	// Doesn't touch the GPU yet; just tells the driver who we are, which API
	// version we want, and which extensions (surface, to attach a window) we need.
	// Debug builds also attach a validation layer to catch mistakes.
	// Note: the `p` prefix means pointer, `pp` means pointer-to-pointer (i.e. an
	// array of strings) — this convention is everywhere in Vulkan, don't panic.
	// =====================================================================
	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = "HeliosEngine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "HeliosEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_4;

	// Required extensions: bridge Vulkan to the Win32 window
	std::vector<const char*> instanceExtensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};

	// Debug builds: add the validation layer
	std::vector<const char*> validationLayers;
#ifndef NDEBUG
	instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	validationLayers = {"VK_LAYER_KHRONOS_validation"};
	if (!CheckValidationLayerSupport(validationLayers))
	{
		std::cerr << "[Vulkan] WARNING: Validation layers not available!\n";
		validationLayers.clear();
	}
#endif

	vk::InstanceCreateInfo InstanceCreateInfo;
	InstanceCreateInfo.pApplicationInfo = &appInfo;
	InstanceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(instanceExtensions.size());
	InstanceCreateInfo.ppEnabledExtensionNames = instanceExtensions.data();
	InstanceCreateInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
	InstanceCreateInfo.ppEnabledLayerNames = validationLayers.data();

	m_Instance = vk::createInstanceUnique(InstanceCreateInfo);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Instance.get());
	std::cout << "[Vulkan] Instance created.\n";

	// Debug messenger
#ifndef NDEBUG
	SetupDebugMessenger();
#endif

	// =====================================================================
	// 2. Physical Device — pick a real GPU.
	// There may be several GPUs; enumerate and pick one (prefer discrete), and
	// confirm it supports the swapchain. The key part below is the queue family:
	// layouts differ per GPU, so the graphics family index must be probed, never
	// hardcoded to 0, or you might get a queue that can't draw on another card.
	// =====================================================================
	std::vector<vk::PhysicalDevice> physicalDevices = m_Instance->enumeratePhysicalDevices();
	if (physicalDevices.empty())
	{
		throw std::runtime_error("No Vulkan-capable GPU found!");
	}

	// Prefer a discrete GPU
	for (const auto& dev : physicalDevices)
	{
		vk::PhysicalDeviceProperties props = dev.getProperties();
		if (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu)
		{
			m_PhysicalDevice = dev;
			std::cout << "[Vulkan] GPU: " << props.deviceName << " (discrete)\n";
			break;
		}
	}
	if (!m_PhysicalDevice)
	{
		m_PhysicalDevice = physicalDevices[0];
		vk::PhysicalDeviceProperties props = m_PhysicalDevice.getProperties();
		std::cout << "[Vulkan] GPU: " << props.deviceName << " (fallback)\n";
	}

	// A GPU isn't "one big work pool" — internally it's split into queue families
	// by function. QueueFamilyProperties describes what a family can do, focused on
	// queueFlags (eGraphics/eCompute/eTransfer...) and how many queues it has
	// (queueCount). We scan for the first family that can draw (eGraphics) and
	// record its index, used later when building the device and fetching a queue.
	std::vector<vk::QueueFamilyProperties> queueFamilyProps = m_PhysicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProps.size()); i++)
	{
		if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			m_GraphicsQueueFamily = i;
			break;
		}
	}

	// Confirm the swapchain extension is available
	std::vector<vk::ExtensionProperties> deviceExtensions = m_PhysicalDevice.enumerateDeviceExtensionProperties();
	{
		bool hasSwapchain = false;
		for (const auto& Extension : deviceExtensions)
		{
			if (strcmp(Extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0)
			{
				hasSwapchain = true;
				break;
			}
		}
		if (!hasSwapchain)
		{
			throw std::runtime_error("Device does not support VK_KHR_swapchain!");
		}
	}

	// =====================================================================
	// 3. Logical Device — the real "handle" for creating resources and issuing commands.
	// PhysicalDevice is the GPU itself; LogicalDevice is the operating permission you
	// get. At creation we name the queue family (one queue from the graphics family
	// we probed above) and enable the swapchain extension, then fetch m_GraphicsQueue,
	// the channel used to send commands to the GPU.
	// =====================================================================
	// pQueuePriorities points at a float array of length queueCount. Even for a single
	// queue the API wants a pointer (treat &queuePriority as a 1-element array). To open
	// several queues in one family, pass e.g. {1.0f, 1.0f, 0.5f}. queuePriority must stay
	// alive through the createDevice call (it's called immediately here, so it's safe).
	float queuePriority = 1.0f;
	vk::DeviceQueueCreateInfo QueueCreateInfo;
	QueueCreateInfo.queueFamilyIndex = m_GraphicsQueueFamily;
	QueueCreateInfo.queueCount = 1;
	QueueCreateInfo.pQueuePriorities = &queuePriority;

	std::vector<const char*> deviceExtNames = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

	vk::PhysicalDeviceFeatures deviceFeatures{};

	vk::DeviceCreateInfo DeviceCreateInfo;
	DeviceCreateInfo.queueCreateInfoCount = 1;
	DeviceCreateInfo.pQueueCreateInfos = &QueueCreateInfo;
	DeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtNames.size());
	DeviceCreateInfo.ppEnabledExtensionNames = deviceExtNames.data();
	DeviceCreateInfo.pEnabledFeatures = &deviceFeatures;

	m_Device = m_PhysicalDevice.createDeviceUnique(DeviceCreateInfo);
	VULKAN_HPP_DEFAULT_DISPATCHER.init(m_Device.get());
	m_GraphicsQueue = m_Device->getQueue(m_GraphicsQueueFamily, 0);
	std::cout << "[Vulkan] Device created.\n";

	// =====================================================================
	// 4. Surface — introduce the window to Vulkan.
	// Hand the Win32 window handle (HWND) to Vulkan so it knows which window to
	// present into. It belongs to neither Instance nor Device — it's the middleman
	// connecting them, and the SwapChain must be attached to it.
	// =====================================================================
	vk::Win32SurfaceCreateInfoKHR SurfaceCreateInfo;
	SurfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
	SurfaceCreateInfo.hwnd = InHwnd;

	m_Surface = m_Instance->createWin32SurfaceKHRUnique(SurfaceCreateInfo);
	std::cout << "[Vulkan] Surface created.\n";

	// =====================================================================
	// 5. SwapChain — the "image swap zone" between you and the display.
	// A driver-managed image queue (double/triple buffering): the GPU draws into a
	// back image, then flips it to the front to avoid tearing. Internally it holds
	// several SwapChainImages, each wrapped in an ImageView (see 5e) before it can
	// be a render target. Steps 5a~5d below are really just "choosing parameters":
	// format, present mode, resolution, image count.
	// =====================================================================
	vk::SurfaceCapabilitiesKHR SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface.get());

	// 5a. Format: B8G8R8A8_UNORM (supported by most displays)
	std::vector<vk::SurfaceFormatKHR> surfaceFormats = m_PhysicalDevice.getSurfaceFormatsKHR(m_Surface.get());
	m_SwapChainFormat = vk::Format::eB8G8R8A8Unorm;
	for (const auto& Format : surfaceFormats)
	{
		if (Format.format == vk::Format::eB8G8R8A8Unorm && Format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear)
		{
			m_SwapChainFormat = Format.format;
			break;
		}
	}

	// 5b. Present mode: FIFO = V-Sync (must be supported by all Vulkan implementations)
	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;

	// 5c. Extent: clamp to surface capabilities
	m_SwapChainExtent = SurfaceCapabilities.currentExtent;
	if (m_SwapChainExtent.width == UINT32_MAX)
	{
		// driver didn't specify -> use window size
		m_SwapChainExtent.width = std::clamp(static_cast<uint32_t>(InWidth), SurfaceCapabilities.minImageExtent.width,
											 SurfaceCapabilities.maxImageExtent.width);
		m_SwapChainExtent.height =
			std::clamp(static_cast<uint32_t>(InHeight), SurfaceCapabilities.minImageExtent.height,
					   SurfaceCapabilities.maxImageExtent.height);
	}

	// 5d. Image count: min + 1 (avoids stalling on acquire while the driver flips)
	uint32_t imageCount = SurfaceCapabilities.minImageCount + 1;
	if (SurfaceCapabilities.maxImageCount > 0 && imageCount > SurfaceCapabilities.maxImageCount)
	{
		imageCount = SurfaceCapabilities.maxImageCount;
	}

	vk::SwapchainCreateInfoKHR SwapChainCreateInfo;
	SwapChainCreateInfo.surface = m_Surface.get();
	SwapChainCreateInfo.minImageCount = imageCount;
	SwapChainCreateInfo.imageFormat = m_SwapChainFormat;
	SwapChainCreateInfo.imageColorSpace = vk::ColorSpaceKHR::eSrgbNonlinear;
	SwapChainCreateInfo.imageExtent = m_SwapChainExtent;
	SwapChainCreateInfo.imageArrayLayers = 1;
	SwapChainCreateInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;
	SwapChainCreateInfo.imageSharingMode = vk::SharingMode::eExclusive;
	SwapChainCreateInfo.preTransform = SurfaceCapabilities.currentTransform;
	SwapChainCreateInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
	SwapChainCreateInfo.presentMode = presentMode;
	SwapChainCreateInfo.clipped = VK_TRUE;
	SwapChainCreateInfo.oldSwapchain = nullptr;

	m_SwapChain = m_Device->createSwapchainKHRUnique(SwapChainCreateInfo);
	m_SwapChainImages = m_Device->getSwapchainImagesKHR(m_SwapChain.get());

	// 5e. Image views
	m_SwapChainImageViews.reserve(m_SwapChainImages.size());
	for (const auto& image : m_SwapChainImages)
	{
		vk::ImageViewCreateInfo ViewCreateInfo;
		ViewCreateInfo.image = image;
		ViewCreateInfo.viewType = vk::ImageViewType::e2D;
		ViewCreateInfo.format = m_SwapChainFormat;
		ViewCreateInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
		ViewCreateInfo.subresourceRange.baseMipLevel = 0;
		ViewCreateInfo.subresourceRange.levelCount = 1;
		ViewCreateInfo.subresourceRange.baseArrayLayer = 0;
		ViewCreateInfo.subresourceRange.layerCount = 1;

		m_SwapChainImageViews.push_back(m_Device->createImageViewUnique(ViewCreateInfo));
	}

	std::cout << "[Vulkan] SwapChain: " << m_SwapChainExtent.width << "x" << m_SwapChainExtent.height << ", "
			  << imageCount << " images\n";

	// =====================================================================
	// 6. RenderPass — the "flow blueprint" for drawing a frame (classic style,
	//    not dynamic rendering). It holds no pixels; it just declares to the driver:
	//    this frame has 1 color attachment, cleared at start, stored at end, with
	//    the final layout transitioned to presentable; 1 subpass; and one dependency
	//    guaranteeing "wait for the previous frame to finish before starting this one".
	//    The driver can optimize up front from the blueprint.
	// =====================================================================
	vk::AttachmentDescription colorAttachment;
	colorAttachment.format = m_SwapChainFormat;
	colorAttachment.samples = vk::SampleCountFlagBits::e1;
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;	 // clear at frame start
	colorAttachment.storeOp = vk::AttachmentStoreOp::eStore; // store at frame end
	colorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	colorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	colorAttachment.initialLayout = vk::ImageLayout::eUndefined;
	colorAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

	vk::AttachmentReference colorRef;
	colorRef.attachment = 0; // layout(location=0)
	colorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::SubpassDescription subpass;
	subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass.colorAttachmentCount = 1;
	subpass.pColorAttachments = &colorRef;

	// Subpass dependency: wait for the previous frame's color output to complete
	vk::SubpassDependency dependency;
	dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	dependency.dstSubpass = 0;
	dependency.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.srcAccessMask = vk::AccessFlagBits::eNone;
	dependency.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependency.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

	vk::RenderPassCreateInfo RenderPassCreateInfo;
	RenderPassCreateInfo.attachmentCount = 1;
	RenderPassCreateInfo.pAttachments = &colorAttachment;
	RenderPassCreateInfo.subpassCount = 1;
	RenderPassCreateInfo.pSubpasses = &subpass;
	RenderPassCreateInfo.dependencyCount = 1;
	RenderPassCreateInfo.pDependencies = &dependency;

	m_RenderPass = m_Device->createRenderPassUnique(RenderPassCreateInfo);
	std::cout << "[Vulkan] RenderPass created.\n";

	// =====================================================================
	// 7. Graphics Pipeline — weld "shaders + all render state" into one fixed pipeline.
	// Built once, reused forever: vertex/fragment shaders, plus input assembly,
	// viewport, rasterization, blending, and a pile of other state, all locked into
	// this pipeline. At draw time you just bind it.
	// =====================================================================

	// 7a. First untangle two easily-confused things: ShaderModule and ShaderStage.
	// A Module is just "a chunk of compiled code" (SPIR-V bytecode) — it doesn't
	// know what it's for: not whether vertex or fragment, nor the entry function name.
	// A Stage is the "bookmark": it pins this code to a pipeline stage (stage) and
	// picks the function inside the Module (pName, usually "main"). So we build two
	// code chunks (Modules), then two bookmarks (Stages) tagging them vertex/fragment,
	// and finally hand the bookmark list to the pipeline. setPCode's `p` is pointer to
	// the bytecode; setCodeSize is its byte count. The learning version hardcoded the
	// bytecode as an array; the proper way is to read it from a .spv file and pass a pointer.
	// Load from the on-disk .spv files (HELIOS_SHADER_DIR injected by CMake at compile time)
	const std::string shaderDir = HELIOS_SHADER_DIR "/vulkan/";
	vk::UniqueShaderModule vertModule = LoadShaderModule(m_Device.get(), shaderDir + "triangle.vert.spv");
	vk::UniqueShaderModule fragModule = LoadShaderModule(m_Device.get(), shaderDir + "triangle.frag.spv");

	vk::PipelineShaderStageCreateInfo vertStage;
	vertStage.stage = vk::ShaderStageFlagBits::eVertex;
	vertStage.module = vertModule.get();
	vertStage.pName = "main";

	vk::PipelineShaderStageCreateInfo fragStage;
	fragStage.stage = vk::ShaderStageFlagBits::eFragment;
	fragStage.module = fragModule.get();
	fragStage.pName = "main";

	// The two bookmarks form the shader list the pipeline ultimately uses
	std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertStage, fragStage};

	// 7b. Vertex input: no vertex buffer, all positions are hardcoded in the VS
	vk::PipelineVertexInputStateCreateInfo vertexInput;

	// 7c. Input assembly
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

	// 7d. Viewport + Scissor: set as dynamic, provided via cmd at runtime
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// 7e. Rasterizer
	vk::PipelineRasterizationStateCreateInfo rasterizer;
	rasterizer.polygonMode = vk::PolygonMode::eFill;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = vk::CullModeFlagBits::eNone; // don't cull the triangle
	rasterizer.frontFace = vk::FrontFace::eClockwise;

	// 7f. Multisampling: off
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

	// 7g. Color blending: plain overwrite, no alpha blending
	vk::PipelineColorBlendAttachmentState blendAttachment;
	blendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
									 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;

	vk::PipelineColorBlendStateCreateInfo colorBlend;
	colorBlend.attachmentCount = 1;
	colorBlend.pAttachments = &blendAttachment;

	// 7h. Dynamic states
	std::vector<vk::DynamicState> dynamicStates = {
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	};
	vk::PipelineDynamicStateCreateInfo dynamicState;
	dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
	dynamicState.pDynamicStates = dynamicStates.data();

	// 7i. m_PipelineLayout: declare a push constant range for the vertex shader to read
	// per-object transforms. Push constants are a small GPU region (typically <=128 bytes)
	// the shader can read directly, written via pushConstants() during recording. We open
	// sizeof(TrianglePush) bytes here.
	vk::PushConstantRange pushRange;
	pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
	pushRange.offset = 0;
	pushRange.size = sizeof(TrianglePush);

	vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo;
	PipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	PipelineLayoutCreateInfo.pPushConstantRanges = &pushRange;
	m_PipelineLayout = m_Device->createPipelineLayoutUnique(PipelineLayoutCreateInfo);

	// 7j. Graphics pipeline
	vk::GraphicsPipelineCreateInfo PipelineCreateInfo;
	PipelineCreateInfo.stageCount = static_cast<uint32_t>(stages.size());
	PipelineCreateInfo.pStages = stages.data();
	PipelineCreateInfo.pVertexInputState = &vertexInput;
	PipelineCreateInfo.pInputAssemblyState = &inputAssembly;
	PipelineCreateInfo.pViewportState = &viewportState;
	PipelineCreateInfo.pRasterizationState = &rasterizer;
	PipelineCreateInfo.pMultisampleState = &multisampling;
	PipelineCreateInfo.pColorBlendState = &colorBlend;
	PipelineCreateInfo.pDynamicState = &dynamicState;
	PipelineCreateInfo.layout = m_PipelineLayout.get();
	PipelineCreateInfo.renderPass = m_RenderPass.get();
	PipelineCreateInfo.subpass = 0;

	vk::ResultValue<vk::UniquePipeline> pipeResult =
		m_Device->createGraphicsPipelineUnique(nullptr, PipelineCreateInfo);
	if (pipeResult.result != vk::Result::eSuccess)
	{
		throw std::runtime_error("Failed to create graphics pipeline!");
	}
	m_Pipeline = std::move(pipeResult.value);
	std::cout << "[Vulkan] Graphics pipeline created.\n";

	// =====================================================================
	// 8. Framebuffers — fill the blueprint with concrete canvases.
	// The RenderPass only says "I need 1 color attachment"; the Framebuffer is what
	// actually specifies "this attachment is SwapChain image N". One per SwapChain
	// image, picked by index during command recording.
	// =====================================================================
	m_Framebuffers.reserve(m_SwapChainImageViews.size());
	for (const auto& view : m_SwapChainImageViews)
	{
		vk::FramebufferCreateInfo FramebufferCreateInfo;
		FramebufferCreateInfo.renderPass = m_RenderPass.get();
		FramebufferCreateInfo.attachmentCount = 1;
		FramebufferCreateInfo.pAttachments = &view.get();
		FramebufferCreateInfo.width = m_SwapChainExtent.width;
		FramebufferCreateInfo.height = m_SwapChainExtent.height;
		FramebufferCreateInfo.layers = 1;

		m_Framebuffers.push_back(m_Device->createFramebufferUnique(FramebufferCreateInfo));
	}
	std::cout << "[Vulkan] Framebuffers: " << m_Framebuffers.size() << "\n";

	// =====================================================================
	// 9. CommandPool + CommandBuffer — record the "tape" first, play it all at once.
	// Vulkan is a record-then-submit model: you can't just order a draw directly; you
	// record all commands into a CommandBuffer, then submit the whole thing to a queue
	// for the GPU to execute. The CommandPool is the memory allocator for these tapes
	// (partitioned by queue family). We build the pool here and grab one tape.
	// =====================================================================
	vk::CommandPoolCreateInfo PoolCreateInfo;
	PoolCreateInfo.queueFamilyIndex = m_GraphicsQueueFamily;
	PoolCreateInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

	m_CommandPool = m_Device->createCommandPoolUnique(PoolCreateInfo);

	vk::CommandBufferAllocateInfo CommandAllocInfo;
	CommandAllocInfo.commandPool = m_CommandPool.get();
	CommandAllocInfo.level = vk::CommandBufferLevel::ePrimary;
	CommandAllocInfo.commandBufferCount = 1;

	std::vector<vk::CommandBuffer> cmdBufs = m_Device->allocateCommandBuffers(CommandAllocInfo);
	m_CommandBuffer = cmdBufs[0];
	std::cout << "[Vulkan] Command pool + buffer ready.\n";

	// =====================================================================
	// 10. Two Semaphores — the "traffic lights" between GPU stages.
	// They live inside the GPU; the CPU can't wait on them directly. imageAvailable
	// means "this image is yours to draw into", renderFinished means "I'm done, show
	// it". At Submit they're chained: wait for imageAvailable before starting, light
	// renderFinished when done, and Present waits on that.
	// =====================================================================
	m_ImageAvailableSemaphore = m_Device->createSemaphoreUnique({});
	m_RenderFinishedSemaphore = m_Device->createSemaphoreUnique({});

	std::cout << "[Vulkan] Initialization complete.\n";
}

// =========================================================================
// SetupDebugMessenger
// =========================================================================
#ifndef NDEBUG
void VulkanRenderer::SetupDebugMessenger()
{
	vk::DebugUtilsMessengerCreateInfoEXT DebugCreateInfo;
	DebugCreateInfo.messageSeverity =
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo |
		vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
	DebugCreateInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
								  vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
								  vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
	DebugCreateInfo.pfnUserCallback = reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(DebugCallback);

	m_DebugMessenger = m_Instance->createDebugUtilsMessengerEXTUnique(DebugCreateInfo);
	std::cout << "[Vulkan] Debug messenger attached.\n";
}
#endif

// =========================================================================
// RecordCommandBuffer — record a frame's full set of draw commands
// =========================================================================
void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex)
{
	m_CommandBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	m_CommandBuffer.begin(beginInfo);

	// Clear color: dark blue-black background
	vk::ClearValue clearColor;
	clearColor.color = vk::ClearColorValue(std::array<float, 4>{0.02f, 0.02f, 0.06f, 1.0f});

	// --- Begin RenderPass ---
	vk::RenderPassBeginInfo RenderPassBeginInfo;
	RenderPassBeginInfo.renderPass = m_RenderPass.get();
	RenderPassBeginInfo.framebuffer = m_Framebuffers[imageIndex].get();
	RenderPassBeginInfo.renderArea.offset = vk::Offset2D{0, 0};
	RenderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
	RenderPassBeginInfo.clearValueCount = 1;
	RenderPassBeginInfo.pClearValues = &clearColor;

	m_CommandBuffer.beginRenderPass(RenderPassBeginInfo, vk::SubpassContents::eInline);

	// --- Bind pipeline ---
	m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_Pipeline.get());

	// --- Dynamic state: viewport ---
	vk::Viewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(m_SwapChainExtent.width);
	viewport.height = static_cast<float>(m_SwapChainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	m_CommandBuffer.setViewport(0, 1, &viewport);

	// --- Dynamic state: scissor ---
	vk::Rect2D scissor;
	scissor.offset = vk::Offset2D{0, 0};
	scissor.extent = m_SwapChainExtent;
	m_CommandBuffer.setScissor(0, 1, &scissor);

	// --- Draw multiple objects ---
	// Key point: RenderPass / Framebuffer / Pipeline are all unchanged. "Multiple
	// objects" happens entirely here — the same pipeline, draw N times in a loop,
	// each time pushing a different offset/scale via pushConstants so the same
	// triangle lands at a different position/size. A real engine would swap
	// offset/scale for each mesh's MVP matrix and add a vertex buffer.
	const std::array<TrianglePush, 5> objects = {{
		{{0.0f, 0.0f}, 1.0f},	// center, full size
		{{-0.6f, -0.5f}, 0.4f}, // top-left, shrunk
		{{0.6f, -0.5f}, 0.4f},	// top-right, shrunk
		{{-0.6f, 0.5f}, 0.4f},	// bottom-left, shrunk
		{{0.6f, 0.5f}, 0.4f},	// bottom-right, shrunk
	}};

	for (const auto& obj : objects)
	{
		m_CommandBuffer.pushConstants<TrianglePush>(m_PipelineLayout.get(), vk::ShaderStageFlagBits::eVertex,
													0, obj);
		m_CommandBuffer.draw(3, 1, 0, 0); // 3 vertices, 1 instance, no vertex buffer
	}

	// --- End ---
	m_CommandBuffer.endRenderPass();
	m_CommandBuffer.end();
}

// =========================================================================
// Render — called once per frame
//
// 5-step pipeline; semaphores chain up the in-GPU sync:
//   Acquire -> Record -> Submit -> Present -> waitIdle (temporary)
//
// A Semaphore is a GPU-GPU signal: the CPU doesn't block, it only tells the GPU
// "wait for that signal to light up, then do the work". This differs from a Fence
// (a CPU-GPU signal) — a Fence lets the CPU wait on the GPU, a Semaphore cannot.
// =========================================================================
void VulkanRenderer::Render()
{
	// 1. Acquire — ask the SwapChain for a drawable Image. The Image might still be
	//    in use by the display (last frame), so the driver blocks until one is free.
	//    Once obtained, the GPU signals on m_ImageAvailableSemaphore.
	uint32_t imageIndex;
	vk::Result acquireResult = m_Device->acquireNextImageKHR(m_SwapChain.get(), UINT64_MAX,
															 m_ImageAvailableSemaphore.get(), nullptr, &imageIndex);

	if (acquireResult == vk::Result::eErrorOutOfDateKHR || acquireResult == vk::Result::eSuboptimalKHR)
	{
		std::cerr << "[Vulkan] SwapChain out of date (window resize?)\n";
		return;
	}
	if (acquireResult != vk::Result::eSuccess)
	{
		throw std::runtime_error("acquireNextImageKHR failed!");
	}

	// 2. Record
	RecordCommandBuffer(imageIndex);

	// 3. Submit — hand the recorded CommandBuffer to the GPU queue for execution.
	//    Wait until m_ImageAvailableSemaphore lights (image is usable) before drawing;
	//    once done, light m_RenderFinishedSemaphore (tell the display it can flip).
	//    pWaitDstStageMask picks "which pipeline stage waits on this signal" — we
	//    choose ColorAttachmentOutput because we only need color output and don't
	//    need to wait on vertex/fragment stages (those don't touch the swapchain image).
	vk::PipelineStageFlags waitStage = vk::PipelineStageFlagBits::eColorAttachmentOutput;

	vk::SubmitInfo submit;
	submit.waitSemaphoreCount = 1;
	submit.pWaitSemaphores = &m_ImageAvailableSemaphore.get();
	submit.pWaitDstStageMask = &waitStage;
	submit.commandBufferCount = 1;
	submit.pCommandBuffers = &m_CommandBuffer;
	submit.signalSemaphoreCount = 1;
	submit.pSignalSemaphores = &m_RenderFinishedSemaphore.get();

	m_GraphicsQueue.submit(submit, nullptr);

	// 4. Present — once the GPU is done (m_RenderFinishedSemaphore lit), flip to screen
	vk::PresentInfoKHR present;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &m_RenderFinishedSemaphore.get();
	present.swapchainCount = 1;
	present.pSwapchains = &m_SwapChain.get();
	present.pImageIndices = &imageIndex;

	vk::Result presentResult = m_GraphicsQueue.presentKHR(&present);

	if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
	{
		return;
	}
	if (presentResult != vk::Result::eSuccess)
	{
		throw std::runtime_error("presentKHR failed!");
	}

	// 5. Wait: simple sync — block until the GPU finishes everything before next frame
	//    A later phase upgrades this to Fence + multi-frame parallelism.
	m_Device->waitIdle();
}

// =========================================================================
// Shutdown — explicitly destroy resources in reverse order
// =========================================================================
void VulkanRenderer::Shutdown()
{
	if (!m_Device)
		return;

	// Wait for the GPU to finish all work
	m_Device->waitIdle();

	// Explicit reverse-order release (UniqueHandles auto-free on destruction; calling
	// reset explicitly just helps debugging)
	m_ImageAvailableSemaphore.reset();
	m_RenderFinishedSemaphore.reset();
	m_CommandPool.reset();
	m_Framebuffers.clear();
	m_Pipeline.reset();
	m_PipelineLayout.reset();
	m_RenderPass.reset();
	m_SwapChainImageViews.clear();
	m_SwapChain.reset();
	m_Surface.reset();
#ifndef NDEBUG
	m_DebugMessenger.reset();
#endif
	m_Device.reset();
	m_Instance.reset();

	std::cout << "[Vulkan] Shutdown complete.\n";
}

} // namespace Helios
