#include "VulkanRenderer.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
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
	std::ifstream file(path, std::ios::ate | std::ios::binary);
	if (!file.is_open())
		throw std::runtime_error("Failed to open shader file: " + path);

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

// One vertex of the base triangle. Field layout must match the
// VertexInputAttributeDescription (location 0 = pos, location 1 = color) and the
// `in` declarations in triangle.vert.
struct Vertex
{
	float pos[2];
	float color[3];
};

// The base triangle geometry (same shape/colors that used to be hardcoded in the VS).
// Uploaded once into the vertex buffer; the 5 draws reuse it with different push constants.
static const std::array<Vertex, 3> g_TriangleVertices = {{
	{{0.0f, -0.5f}, {1.0f, 0.0f, 0.0f}}, // top    — red
	{{0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}},	 // bottom-right — green
	{{-0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}}, // bottom-left  — blue
}};

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
		throw std::runtime_error("Failed to load vulkan-1.dll!");

	auto vkGetInstanceProcAddr =
		reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkanDll, "vkGetInstanceProcAddr"));
	if (!vkGetInstanceProcAddr)
		throw std::runtime_error("Failed to get vkGetInstanceProcAddr!");

	VULKAN_HPP_DEFAULT_DISPATCHER.init(vkGetInstanceProcAddr);

	// =====================================================================
	// 1. Instance — the "first handshake" with the Vulkan driver.
	// =====================================================================
	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = "HeliosEngine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "HeliosEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_4;

	std::vector<const char*> instanceExtensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};

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

#ifndef NDEBUG
	SetupDebugMessenger();
#endif

	// =====================================================================
	// 2. Physical Device — pick a real GPU.
	// =====================================================================
	std::vector<vk::PhysicalDevice> physicalDevices = m_Instance->enumeratePhysicalDevices();
	if (physicalDevices.empty())
		throw std::runtime_error("No Vulkan-capable GPU found!");

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

	std::vector<vk::QueueFamilyProperties> queueFamilyProps = m_PhysicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProps.size()); i++)
	{
		if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			m_GraphicsQueueFamily = i;
			break;
		}
	}

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
			throw std::runtime_error("Device does not support VK_KHR_swapchain!");
	}

	// =====================================================================
	// 3. Logical Device
	// =====================================================================
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
	// 4. Surface
	// =====================================================================
	vk::Win32SurfaceCreateInfoKHR SurfaceCreateInfo;
	SurfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
	SurfaceCreateInfo.hwnd = InHwnd;

	m_Surface = m_Instance->createWin32SurfaceKHRUnique(SurfaceCreateInfo);
	std::cout << "[Vulkan] Surface created.\n";

	// =====================================================================
	// 5. SwapChain
	// =====================================================================
	vk::SurfaceCapabilitiesKHR SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface.get());

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

	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;

	m_SwapChainExtent = SurfaceCapabilities.currentExtent;
	if (m_SwapChainExtent.width == UINT32_MAX)
	{
		m_SwapChainExtent.width = std::clamp(static_cast<uint32_t>(InWidth), SurfaceCapabilities.minImageExtent.width,
											 SurfaceCapabilities.maxImageExtent.width);
		m_SwapChainExtent.height =
			std::clamp(static_cast<uint32_t>(InHeight), SurfaceCapabilities.minImageExtent.height,
					   SurfaceCapabilities.maxImageExtent.height);
	}

	uint32_t imageCount = SurfaceCapabilities.minImageCount + 1;
	if (SurfaceCapabilities.maxImageCount > 0 && imageCount > SurfaceCapabilities.maxImageCount)
		imageCount = SurfaceCapabilities.maxImageCount;

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
	// 5f. Offscreen intermediate SceneColor — the heart of the 2-subpass demo.
	// subpass 0 writes the triangles here, subpass 1 reads it as an input
	// attachment. It is never presented; it is purely an in-pipeline scratch
	// canvas. usage carries both ColorAttachment (written) and InputAttachment (read).
	// =====================================================================
	m_SceneColorImage = m_Device->createImageUnique(vk::ImageCreateInfo{
		{}, vk::ImageType::e2D, m_SwapChainFormat,
		vk::Extent3D{m_SwapChainExtent.width, m_SwapChainExtent.height, 1}, 1, 1,
		vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
		vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eInputAttachment,
		vk::SharingMode::eExclusive, 0, nullptr, vk::ImageLayout::eUndefined});

	vk::MemoryRequirements SceneMemReq = m_Device->getImageMemoryRequirements(m_SceneColorImage.get());
	m_SceneColorMemory = m_Device->allocateMemoryUnique(vk::MemoryAllocateInfo{
		SceneMemReq.size, FindMemoryType(SceneMemReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
	m_Device->bindImageMemory(m_SceneColorImage.get(), m_SceneColorMemory.get(), 0);

	vk::ImageViewCreateInfo SceneViewInfo;
	SceneViewInfo.image = m_SceneColorImage.get();
	SceneViewInfo.viewType = vk::ImageViewType::e2D;
	SceneViewInfo.format = m_SwapChainFormat;
	SceneViewInfo.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
	SceneViewInfo.subresourceRange.baseMipLevel = 0;
	SceneViewInfo.subresourceRange.levelCount = 1;
	SceneViewInfo.subresourceRange.baseArrayLayer = 0;
	SceneViewInfo.subresourceRange.layerCount = 1;
	m_SceneColorView = m_Device->createImageViewUnique(SceneViewInfo);
	std::cout << "[Vulkan] SceneColor (offscreen) image created.\n";

	// =====================================================================
	// 5g. Vertex buffer — the base triangle's geometry now lives in GPU memory.
	// Host-visible + coherent memory keeps upload trivial (map -> memcpy -> unmap),
	// no staging buffer needed. Fine for a handful of static vertices; a later phase
	// can switch to a device-local buffer with a staging copy for larger meshes.
	// =====================================================================
	vk::DeviceSize vertexBufferSize = sizeof(g_TriangleVertices[0]) * g_TriangleVertices.size();

	m_VertexBuffer = m_Device->createBufferUnique(vk::BufferCreateInfo{
		{}, vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer, vk::SharingMode::eExclusive});

	vk::MemoryRequirements vbMemReq = m_Device->getBufferMemoryRequirements(m_VertexBuffer.get());
	m_VertexBufferMemory = m_Device->allocateMemoryUnique(vk::MemoryAllocateInfo{
		vbMemReq.size, FindMemoryType(vbMemReq.memoryTypeBits,
									  vk::MemoryPropertyFlagBits::eHostVisible |
										  vk::MemoryPropertyFlagBits::eHostCoherent)});
	m_Device->bindBufferMemory(m_VertexBuffer.get(), m_VertexBufferMemory.get(), 0);

	// Map the memory, copy the vertices in, unmap. HostCoherent means no explicit flush needed.
	void* vbData = m_Device->mapMemory(m_VertexBufferMemory.get(), 0, vertexBufferSize);
	std::memcpy(vbData, g_TriangleVertices.data(), static_cast<size_t>(vertexBufferSize));
	m_Device->unmapMemory(m_VertexBufferMemory.get());
	std::cout << "[Vulkan] Vertex buffer created and uploaded.\n";

	// =====================================================================
	// 6. RenderPass — the 2-subpass "flow blueprint".
	// subpass 0: renders the 5 triangles into SceneColor (attachment slot 0).
	// subpass 1: reads SceneColor as input attachment, runs post, writes SwapChain (slot 1).
	// The blueprint only names slots; the framebuffer later binds real images.
	// =====================================================================
	vk::AttachmentDescription attachments[2];

	// Slot 0: SceneColor (offscreen). Clear at start, store at end so subpass 1 can read it.
	attachments[0].format = m_SwapChainFormat;
	attachments[0].samples = vk::SampleCountFlagBits::e1;
	attachments[0].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[0].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[0].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[0].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[0].initialLayout = vk::ImageLayout::eUndefined;
	attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal; // stays offscreen, never presented

	// Slot 1: SwapChain (screen). Clear at start, transition to presentable at end.
	attachments[1].format = m_SwapChainFormat;
	attachments[1].samples = vk::SampleCountFlagBits::e1;
	attachments[1].loadOp = vk::AttachmentLoadOp::eClear;
	attachments[1].storeOp = vk::AttachmentStoreOp::eStore;
	attachments[1].stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
	attachments[1].stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
	attachments[1].initialLayout = vk::ImageLayout::eUndefined;
	attachments[1].finalLayout = vk::ImageLayout::ePresentSrcKHR;

	// subpass 0: write SceneColor (slot 0)
	vk::AttachmentReference colorRef0;
	colorRef0.attachment = 0;
	colorRef0.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::SubpassDescription subpass0;
	subpass0.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass0.colorAttachmentCount = 1;
	subpass0.pColorAttachments = &colorRef0;

	// subpass 1: write SwapChain (slot 1), read SceneColor (slot 0) as input
	vk::AttachmentReference colorRef1;
	colorRef1.attachment = 1;
	colorRef1.layout = vk::ImageLayout::eColorAttachmentOptimal;

	vk::AttachmentReference inputRef0;
	inputRef0.attachment = 0;
	inputRef0.layout = vk::ImageLayout::eShaderReadOnlyOptimal;

	vk::SubpassDescription subpass1;
	subpass1.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
	subpass1.colorAttachmentCount = 1;
	subpass1.pColorAttachments = &colorRef1;
	subpass1.inputAttachmentCount = 1;
	subpass1.pInputAttachments = &inputRef0;

	// Two dependencies:
	//   EXTERNAL -> subpass0: wait for acquire before drawing (same as the single-pass build).
	//   subpass0 -> subpass1: wait until subpass 0 has finished writing SceneColor
	//                       before subpass 1 reads it as an input attachment.
	std::array<vk::SubpassDependency, 2> dependencies;
	dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
	dependencies[0].dstSubpass = 0;
	dependencies[0].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].srcAccessMask = vk::AccessFlagBits::eNone;
	dependencies[0].dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[0].dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;

	dependencies[1].srcSubpass = 0;
	dependencies[1].dstSubpass = 1;
	dependencies[1].srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
	dependencies[1].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
	dependencies[1].dstStageMask = vk::PipelineStageFlagBits::eFragmentShader;
	dependencies[1].dstAccessMask = vk::AccessFlagBits::eInputAttachmentRead;

	std::array<vk::SubpassDescription, 2> subpasses = {subpass0, subpass1};

	vk::RenderPassCreateInfo RenderPassCreateInfo;
	RenderPassCreateInfo.attachmentCount = 2;
	RenderPassCreateInfo.pAttachments = attachments;
	RenderPassCreateInfo.subpassCount = 2;
	RenderPassCreateInfo.pSubpasses = subpasses.data();
	RenderPassCreateInfo.dependencyCount = 2;
	RenderPassCreateInfo.pDependencies = dependencies.data();

	m_RenderPass = m_Device->createRenderPassUnique(RenderPassCreateInfo);
	std::cout << "[Vulkan] RenderPass created (2 subpasses).\n";

	// =====================================================================
	// 7. Base pipeline — 5 triangles, no vertex buffer, push constants for per-object transform.
	// =====================================================================
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

	std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertStage, fragStage};

	// 7b. Vertex input: describe how the vertex buffer maps to shader inputs.
	// Binding = one buffer bound at slot 0, advancing one Vertex per vertex.
	// Attributes = each `in` variable in the VS: its format + byte offset in Vertex.
	vk::VertexInputBindingDescription bindingDesc;
	bindingDesc.binding = 0;
	bindingDesc.stride = sizeof(Vertex);
	bindingDesc.inputRate = vk::VertexInputRate::eVertex;

	std::array<vk::VertexInputAttributeDescription, 2> attrDescs;
	attrDescs[0].location = 0; // matches layout(location = 0) in vec2 inPos
	attrDescs[0].binding = 0;
	attrDescs[0].format = vk::Format::eR32G32Sfloat; // vec2
	attrDescs[0].offset = offsetof(Vertex, pos);
	attrDescs[1].location = 1; // matches layout(location = 1) in vec3 inColor
	attrDescs[1].binding = 0;
	attrDescs[1].format = vk::Format::eR32G32B32Sfloat; // vec3
	attrDescs[1].offset = offsetof(Vertex, color);

	vk::PipelineVertexInputStateCreateInfo vertexInput;
	vertexInput.vertexBindingDescriptionCount = 1;
	vertexInput.pVertexBindingDescriptions = &bindingDesc;
	vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrDescs.size());
	vertexInput.pVertexAttributeDescriptions = attrDescs.data();

	// 7c. Input assembly
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

	// 7d. Viewport + Scissor: dynamic
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// 7e. Rasterizer
	vk::PipelineRasterizationStateCreateInfo rasterizer;
	rasterizer.polygonMode = vk::PolygonMode::eFill;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = vk::CullModeFlagBits::eNone;
	rasterizer.frontFace = vk::FrontFace::eClockwise;

	// 7f. Multisampling: off
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

	// 7g. Color blending: plain overwrite
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

	// 7i. Push constant range for the per-object transform
	vk::PushConstantRange pushRange;
	pushRange.stageFlags = vk::ShaderStageFlagBits::eVertex;
	pushRange.offset = 0;
	pushRange.size = sizeof(TrianglePush);

	vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo;
	PipelineLayoutCreateInfo.pushConstantRangeCount = 1;
	PipelineLayoutCreateInfo.pPushConstantRanges = &pushRange;
	m_PipelineLayout = m_Device->createPipelineLayoutUnique(PipelineLayoutCreateInfo);

	// 7j. Base graphics pipeline (subpass 0)
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
		throw std::runtime_error("Failed to create graphics pipeline!");
	m_Pipeline = std::move(pipeResult.value);
	std::cout << "[Vulkan] Base pipeline created.\n";

	// =====================================================================
	// 7k. Post pipeline (subpass 1) — fullscreen triangle + input attachment.
	// Reads SceneColor via subpassLoad and inverts it, demonstrating a
	// base-pass -> post-pass chained by a subpass dependency.
	// =====================================================================
	vk::UniqueShaderModule postVert = LoadShaderModule(m_Device.get(), shaderDir + "post.vert.spv");
	vk::UniqueShaderModule postFrag = LoadShaderModule(m_Device.get(), shaderDir + "post.frag.spv");

	// An input attachment must be declared through a descriptor set binding.
	vk::DescriptorSetLayoutBinding inputBinding;
	inputBinding.binding = 0;
	inputBinding.descriptorType = vk::DescriptorType::eInputAttachment;
	inputBinding.descriptorCount = 1;
	inputBinding.stageFlags = vk::ShaderStageFlagBits::eFragment;
	m_PostDescriptorSetLayout =
		m_Device->createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo{{}, 1, &inputBinding});

	vk::DescriptorPoolSize poolSize;
	poolSize.type = vk::DescriptorType::eInputAttachment;
	poolSize.descriptorCount = 1;
	m_PostDescriptorPool =
		m_Device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{{}, 1, 1, &poolSize});

	auto postSets = m_Device->allocateDescriptorSetsUnique(
		vk::DescriptorSetAllocateInfo{m_PostDescriptorPool.get(), 1, &m_PostDescriptorSetLayout.get()});
	m_PostDescriptorSet = std::move(postSets[0]);

	// Bind SceneColor as the input attachment at binding 0.
	vk::DescriptorImageInfo inputImageInfo;
	inputImageInfo.imageView = m_SceneColorView.get();
	inputImageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
	vk::WriteDescriptorSet write;
	write.dstSet = m_PostDescriptorSet.get();
	write.dstBinding = 0;
	write.dstArrayElement = 0;
	write.descriptorCount = 1;
	write.descriptorType = vk::DescriptorType::eInputAttachment;
	write.pImageInfo = &inputImageInfo;
	m_Device->updateDescriptorSets(1, &write, 0, nullptr);

	m_PostPipelineLayout =
		m_Device->createPipelineLayoutUnique(vk::PipelineLayoutCreateInfo{{}, 1, &m_PostDescriptorSetLayout.get()});

	vk::PipelineShaderStageCreateInfo postVertStage;
	postVertStage.stage = vk::ShaderStageFlagBits::eVertex;
	postVertStage.module = postVert.get();
	postVertStage.pName = "main";

	vk::PipelineShaderStageCreateInfo postFragStage;
	postFragStage.stage = vk::ShaderStageFlagBits::eFragment;
	postFragStage.module = postFrag.get();
	postFragStage.pName = "main";

	std::vector<vk::PipelineShaderStageCreateInfo> postStages = {postVertStage, postFragStage};

	vk::PipelineVertexInputStateCreateInfo postVertexInput;
	vk::PipelineInputAssemblyStateCreateInfo postInputAssembly;
	postInputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

	vk::PipelineViewportStateCreateInfo postViewportState;
	postViewportState.viewportCount = 1;
	postViewportState.scissorCount = 1;

	vk::PipelineRasterizationStateCreateInfo postRasterizer;
	postRasterizer.polygonMode = vk::PolygonMode::eFill;
	postRasterizer.lineWidth = 1.0f;
	postRasterizer.cullMode = vk::CullModeFlagBits::eNone;
	postRasterizer.frontFace = vk::FrontFace::eClockwise;

	vk::PipelineMultisampleStateCreateInfo postMultisampling;
	postMultisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

	vk::PipelineColorBlendAttachmentState postBlendAttachment;
	postBlendAttachment.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG |
										 vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA;
	vk::PipelineColorBlendStateCreateInfo postColorBlend;
	postColorBlend.attachmentCount = 1;
	postColorBlend.pAttachments = &postBlendAttachment;

	std::vector<vk::DynamicState> postDynamicStates = {vk::DynamicState::eViewport, vk::DynamicState::eScissor};
	vk::PipelineDynamicStateCreateInfo postDynamicState;
	postDynamicState.dynamicStateCount = 2;
	postDynamicState.pDynamicStates = postDynamicStates.data();

	vk::GraphicsPipelineCreateInfo postPipelineCreateInfo;
	postPipelineCreateInfo.stageCount = 2;
	postPipelineCreateInfo.pStages = postStages.data();
	postPipelineCreateInfo.pVertexInputState = &postVertexInput;
	postPipelineCreateInfo.pInputAssemblyState = &postInputAssembly;
	postPipelineCreateInfo.pViewportState = &postViewportState;
	postPipelineCreateInfo.pRasterizationState = &postRasterizer;
	postPipelineCreateInfo.pMultisampleState = &postMultisampling;
	postPipelineCreateInfo.pColorBlendState = &postColorBlend;
	postPipelineCreateInfo.pDynamicState = &postDynamicState;
	postPipelineCreateInfo.layout = m_PostPipelineLayout.get();
	postPipelineCreateInfo.renderPass = m_RenderPass.get();
	postPipelineCreateInfo.subpass = 1;

	auto postPipeResult = m_Device->createGraphicsPipelineUnique(nullptr, postPipelineCreateInfo);
	if (postPipeResult.result != vk::Result::eSuccess)
		throw std::runtime_error("Failed to create post pipeline!");
	m_PostPipeline = std::move(postPipeResult.value);
	std::cout << "[Vulkan] Post pipeline created.\n";

	// =====================================================================
	// 8. Framebuffers — fill the blueprint with concrete canvases.
	// Each framebuffer binds 2 views: slot 0 is the shared offscreen SceneColor,
	// slot 1 is SwapChain image N. Picked by index during recording.
	// =====================================================================
	m_Framebuffers.reserve(m_SwapChainImageViews.size());
	for (const auto& view : m_SwapChainImageViews)
	{
		std::array<vk::ImageView, 2> fbAttachments = {m_SceneColorView.get(), view.get()};

		vk::FramebufferCreateInfo FramebufferCreateInfo;
		FramebufferCreateInfo.renderPass = m_RenderPass.get();
		FramebufferCreateInfo.attachmentCount = 2;
		FramebufferCreateInfo.pAttachments = fbAttachments.data();
		FramebufferCreateInfo.width = m_SwapChainExtent.width;
		FramebufferCreateInfo.height = m_SwapChainExtent.height;
		FramebufferCreateInfo.layers = 1;

		m_Framebuffers.push_back(m_Device->createFramebufferUnique(FramebufferCreateInfo));
	}
	std::cout << "[Vulkan] Framebuffers: " << m_Framebuffers.size() << "\n";

	// =====================================================================
	// 9. CommandPool + CommandBuffer
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
	// 10. Two Semaphores
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

	// Two clear colors: SceneColor cleared black, SwapChain cleared dark blue-black.
	std::array<vk::ClearValue, 2> clearValues;
	clearValues[0].color = vk::ClearColorValue(std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f});
	clearValues[1].color = vk::ClearColorValue(std::array<float, 4>{0.02f, 0.02f, 0.06f, 1.0f});

	vk::RenderPassBeginInfo RenderPassBeginInfo;
	RenderPassBeginInfo.renderPass = m_RenderPass.get();
	RenderPassBeginInfo.framebuffer = m_Framebuffers[imageIndex].get();
	RenderPassBeginInfo.renderArea.offset = vk::Offset2D{0, 0};
	RenderPassBeginInfo.renderArea.extent = m_SwapChainExtent;
	RenderPassBeginInfo.clearValueCount = 2;
	RenderPassBeginInfo.pClearValues = clearValues.data();

	m_CommandBuffer.beginRenderPass(RenderPassBeginInfo, vk::SubpassContents::eInline);

	// ===== subpass 0: draw 5 triangles into SceneColor (offscreen) =====
	m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_Pipeline.get());

	vk::Viewport viewport;
	viewport.x = 0.0f;
	viewport.y = 0.0f;
	viewport.width = static_cast<float>(m_SwapChainExtent.width);
	viewport.height = static_cast<float>(m_SwapChainExtent.height);
	viewport.minDepth = 0.0f;
	viewport.maxDepth = 1.0f;
	m_CommandBuffer.setViewport(0, 1, &viewport);

	vk::Rect2D scissor;
	scissor.offset = vk::Offset2D{0, 0};
	scissor.extent = m_SwapChainExtent;
	m_CommandBuffer.setScissor(0, 1, &scissor);

	// Bind the vertex buffer at slot 0; the draw below pulls its 3 vertices from here.
	vk::DeviceSize vbOffsets[] = {0};
	m_CommandBuffer.bindVertexBuffers(0, 1, &m_VertexBuffer.get(), vbOffsets);

	// Multi-object: same pipeline, draw N times; each iteration pushes a different
	// offset/scale so the same triangle lands at a different position/size.
	const std::array<TrianglePush, 5> objects = {{
		{{0.0f, 0.0f}, 1.0f},	 // center, full size
		{{-0.6f, -0.5f}, 0.4f}, // top-left, shrunk
		{{0.6f, -0.5f}, 0.4f},	 // top-right, shrunk
		{{-0.6f, 0.5f}, 0.4f},	 // bottom-left, shrunk
		{{0.6f, 0.5f}, 0.4f},	 // bottom-right, shrunk
	}};

	for (const auto& obj : objects)
	{
		m_CommandBuffer.pushConstants<TrianglePush>(m_PipelineLayout.get(), vk::ShaderStageFlagBits::eVertex, 0, obj);
		m_CommandBuffer.draw(3, 1, 0, 0);
	}

	// ===== subpass 1: post process — read SceneColor, write SwapChain =====
	m_CommandBuffer.nextSubpass(vk::SubpassContents::eInline);

	m_CommandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_PostPipeline.get());
	// Input attachment is bound through the descriptor set at binding 0.
	m_CommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_PostPipelineLayout.get(), 0, 1,
									  &m_PostDescriptorSet.get(), 0, nullptr);
	// Re-set viewport/scissor (the fullscreen triangle covers the whole screen).
	m_CommandBuffer.setViewport(0, 1, &viewport);
	m_CommandBuffer.setScissor(0, 1, &scissor);
	// 3 vertices, 1 instance — post.frag uses subpassLoad to read SceneColor and inverts it.
	m_CommandBuffer.draw(3, 1, 0, 0);

	m_CommandBuffer.endRenderPass();
	m_CommandBuffer.end();
}

// =========================================================================
// Render — called once per frame
// =========================================================================
void VulkanRenderer::Render()
{
	uint32_t imageIndex;
	vk::Result acquireResult = m_Device->acquireNextImageKHR(m_SwapChain.get(), UINT64_MAX,
															 m_ImageAvailableSemaphore.get(), nullptr, &imageIndex);

	if (acquireResult == vk::Result::eErrorOutOfDateKHR || acquireResult == vk::Result::eSuboptimalKHR)
	{
		std::cerr << "[Vulkan] SwapChain out of date (window resize?)\n";
		return;
	}
	if (acquireResult != vk::Result::eSuccess)
		throw std::runtime_error("acquireNextImageKHR failed!");

	RecordCommandBuffer(imageIndex);

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

	vk::PresentInfoKHR present;
	present.waitSemaphoreCount = 1;
	present.pWaitSemaphores = &m_RenderFinishedSemaphore.get();
	present.swapchainCount = 1;
	present.pSwapchains = &m_SwapChain.get();
	present.pImageIndices = &imageIndex;

	vk::Result presentResult = m_GraphicsQueue.presentKHR(&present);

	if (presentResult == vk::Result::eErrorOutOfDateKHR || presentResult == vk::Result::eSuboptimalKHR)
		return;
	if (presentResult != vk::Result::eSuccess)
		throw std::runtime_error("presentKHR failed!");

	// Simple sync for now; a later phase upgrades this to Fences + multi-frame.
	m_Device->waitIdle();
}

// =========================================================================
// Shutdown — explicitly destroy resources in reverse order
// =========================================================================
void VulkanRenderer::Shutdown()
{
	if (!m_Device)
		return;

	m_Device->waitIdle();

	m_ImageAvailableSemaphore.reset();
	m_RenderFinishedSemaphore.reset();
	m_CommandPool.reset();
	m_Framebuffers.clear();
	m_PostPipeline.reset();
	m_PostPipelineLayout.reset();
	m_PostDescriptorSet.reset();
	m_PostDescriptorPool.reset();
	m_PostDescriptorSetLayout.reset();
	m_Pipeline.reset();
	m_PipelineLayout.reset();
	m_VertexBuffer.reset();
	m_VertexBufferMemory.reset();
	m_RenderPass.reset();
	m_SceneColorView.reset();
	m_SceneColorImage.reset();
	m_SceneColorMemory.reset();
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

uint32_t VulkanRenderer::FindMemoryType(uint32_t TypeFilter, vk::MemoryPropertyFlags Properties)
{
	vk::PhysicalDeviceMemoryProperties MemProps = m_PhysicalDevice.getMemoryProperties();
	for (uint32_t i = 0; i < MemProps.memoryTypeCount; i++)
	{
		if ((TypeFilter & (1u << i)) && (MemProps.memoryTypes[i].propertyFlags & Properties) == Properties)
			return i;
	}
	throw std::runtime_error("Failed to find suitable memory type!");
}

} // namespace Helios
