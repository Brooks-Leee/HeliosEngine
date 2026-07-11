#include "VulkanRenderer.h"
#include "triangle_shaders.h" // kVertSPIRV, kFragSPIRV

#include <algorithm>
#include <iostream>
#include <set>
#include <stdexcept>
#include <windows.h>

// VK_NO_PROTOTYPES 模式：需要在恰好一个翻译单元中定义 dispatch loader 的存储
VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

namespace Helios
{

// =========================================================================
// Debug callback — 把 validation layer 消息打印到 stderr
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
// 辅助：检查 validation layer 是否可用
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
// Initialize
// =========================================================================
void VulkanRenderer::Initialize(HWND InHwnd, int InWidth, int InHeight)
{
	// =====================================================================
	// 0. Dispatch Loader
	// Vulkan 的函数不像普通库那样编译期就链好——本项目开了 VK_NO_PROTOTYPES，
	// 所以所有 vkXxx() 都得靠这个全局分发器在运行时去 vulkan-1.dll 里现找。
	// 没有它后面全调不到；而且每建好一个 Instance/Device 都要再 .init 刷新一次。
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
	// 1. Instance —— 你和 Vulkan 驱动之间的"第一次握手"
	// 还不涉及显卡，只是告诉驱动：我是谁、要用哪个 API 版本、需要哪些扩展
	// （surface 扩展用来对接窗口），Debug 版顺手挂上 validation layer 帮你抓错。
	// 小知识：字段名里 p 开头=指针，pp 开头=指针的指针（也就是字符串数组），
	// 这套前缀在 Vulkan 里到处都是，看到别慌。
	// =====================================================================
	vk::ApplicationInfo appInfo;
	appInfo.pApplicationName = "HeliosEngine";
	appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.pEngineName = "HeliosEngine";
	appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
	appInfo.apiVersion = VK_API_VERSION_1_4;

	// 必需扩展：把 Vulkan 和 Win32 窗口关联起来
	std::vector<const char*> instanceExtensions = {
		VK_KHR_SURFACE_EXTENSION_NAME,
		VK_KHR_WIN32_SURFACE_EXTENSION_NAME,
	};

	// Debug 构建：加 validation layer
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
	// 2. Physical Device —— 挑一块真实存在的显卡
	// 系统里可能有多块 GPU，枚举出来选一块（优先独显），顺便确认它支持
	// swapchain。关键是下面的队列族：不同显卡的队列族布局不一样，图形族的下标
	// 必须现探，绝不能写死成 0，否则换张卡就可能拿到不能画图的队列。
	// =====================================================================
	std::vector<vk::PhysicalDevice> physicalDevices = m_Instance->enumeratePhysicalDevices();
	if (physicalDevices.empty())
	{
		throw std::runtime_error("No Vulkan-capable GPU found!");
	}

	// 优先独立显卡
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

	// GPU 不是"一个能干活的大池子"，它内部按功能分成了不同的队列族。
	// QueueFamilyProperties 就是描述"某一族能干啥"的说明书，重点看 queueFlags
	// （eGraphics/eCompute/eTransfer...）和这族有几个队列(queueCount)。
	// 下面就是翻说明书，找出第一个能画图(eGraphics)的族，把它的编号记下来，
	// 后面建设备和取队列都要用。
	std::vector<vk::QueueFamilyProperties> queueFamilyProps = m_PhysicalDevice.getQueueFamilyProperties();
	for (uint32_t i = 0; i < static_cast<uint32_t>(queueFamilyProps.size()); i++)
	{
		if (queueFamilyProps[i].queueFlags & vk::QueueFlagBits::eGraphics)
		{
			m_GraphicsQueueFamily = i;
			break;
		}
	}

	// 确认 swapchain extension 可用
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
	// 3. Logical Device —— 真正能用来建资源、发命令的"操作句柄"
	// Physical Device 是显卡本体，Logical Device 是你拿到的操作权限。
	// 创建时指定要用哪个队列族（从上面探到的 graphics 族开一个队列），
	// 并启用 swapchain 扩展，最后取出 m_GraphicsQueue 这条往 GPU 发命令的通道。
	// =====================================================================
	// pQueuePriorities 指向一个 float 数组，长度 = queueCount。哪怕只开 1 个
	// 队列，API 也要你传指针（把 &queuePriority 当成 1 元素数组即可）。
	// 想同族开多个队列时，就传 {1.0f, 1.0f, 0.5f} 这样的数组。
	// 注意 queuePriority 得活到 createDevice 调用结束（这里紧接着就调用，安全）。
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
	// 4. Surface —— 把窗口"介绍"给 Vulkan
	// 把 Win32 的窗口句柄 HWND 交给 Vulkan，它才知道画面要显示到哪个窗口。
	// 它既不属于 Instance 也不属于 Device，而是连接两者的中间人，SwapChain
	// 必须挂在它上面。
	// =====================================================================
	vk::Win32SurfaceCreateInfoKHR SurfaceCreateInfo;
	SurfaceCreateInfo.hinstance = GetModuleHandle(nullptr);
	SurfaceCreateInfo.hwnd = InHwnd;

	m_Surface = m_Instance->createWin32SurfaceKHRUnique(SurfaceCreateInfo);
	std::cout << "[Vulkan] Surface created.\n";

	// =====================================================================
	// 5. SwapChain —— 你和显示器之间的"画面交换区"
	// 一块由驱动管理的图像队列（双/三缓冲）：GPU 在后台图作画，画完翻到前台
	// 显示，避免画面撕裂。它内部持有若干 SwapChainImages，每张图还要包一层
	// ImageView（见 5e）才能当渲染目标。下面 5a~5d 其实都是在"挑参数"：
	// 格式、显示模式、分辨率、图像数量。
	// =====================================================================
	vk::SurfaceCapabilitiesKHR SurfaceCapabilities = m_PhysicalDevice.getSurfaceCapabilitiesKHR(m_Surface.get());

	// 5a. Format: B8G8R8A8_UNORM（大多数显示器支持）
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

	// 5b. Present mode: FIFO = V-Sync（所有 Vulkan 实现都必须支持）
	vk::PresentModeKHR presentMode = vk::PresentModeKHR::eFifo;

	// 5c. Extent: clamp 到 surface capabilities
	m_SwapChainExtent = SurfaceCapabilities.currentExtent;
	if (m_SwapChainExtent.width == UINT32_MAX)
	{
		// 驱动不指定 → 用窗口大小
		m_SwapChainExtent.width = std::clamp(static_cast<uint32_t>(InWidth), SurfaceCapabilities.minImageExtent.width,
											 SurfaceCapabilities.maxImageExtent.width);
		m_SwapChainExtent.height =
			std::clamp(static_cast<uint32_t>(InHeight), SurfaceCapabilities.minImageExtent.height,
					   SurfaceCapabilities.maxImageExtent.height);
	}

	// 5d. Image count: min + 1（避免 acquire 时等待驱动）
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
	// 6. RenderPass —— 给这一帧画画的"流程蓝图"（传统写法，没用 dynamic rendering）
	// 它不存任何像素，只是向驱动声明：这帧有 1 个颜色附件，开始时清屏、结束时
	// 存盘、最终布局转成可呈现；有 1 个 subpass；并用一条依赖保证"等上一帧
	// 画完再开始本帧"。驱动拿到蓝图能提前做优化。
	// =====================================================================
	vk::AttachmentDescription colorAttachment;
	colorAttachment.format = m_SwapChainFormat;
	colorAttachment.samples = vk::SampleCountFlagBits::e1;
	colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;	 // 开始帧时清屏
	colorAttachment.storeOp = vk::AttachmentStoreOp::eStore; // 结束帧时保存
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

	// Subpass dependency: 等前一个 frame 的 color output 完成
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
	// 7. Graphics Pipeline —— 把"着色器 + 所有渲染状态"焊成一条固定流水线
	// 一次建好、反复用：顶点/片元着色器，加上输入装配、视口、光栅化、混合等
	// 一堆状态，全锁死在这条管线里，绘制时直接 bind 就行。
	// =====================================================================

	// 7a. 先分清两个容易混的东西：ShaderModule 和 ShaderStage
	// Module 只是"一段编译好的代码"（SPIR-V 字节码），它自己不知道该干啥——
	// 不分顶点还是片元，也不知道入口函数叫啥。Stage 才是"书签"：指定这段代码
	// 挂在管线的哪个阶段(stage)、用 Module 里的哪个函数(pName，一般就是 main)。
	// 所以先建两块代码(Module)，再用两个书签(Stage)把它们标成顶点/片元，最后
	// 把书签列表交给管线。setPCode 的 p 就是 pointer，指向那段字节码；
	// setCodeSize 填字节数。学习版直接把字节码硬编码成数组，正经做法是从 .spv
	// 文件读出来再传指针。
	vk::UniqueShaderModule vertModule = m_Device->createShaderModuleUnique(
		vk::ShaderModuleCreateInfo{}.setCodeSize(sizeof(kVertSPIRV)).setPCode(kVertSPIRV));

	vk::UniqueShaderModule fragModule = m_Device->createShaderModuleUnique(
		vk::ShaderModuleCreateInfo{}.setCodeSize(sizeof(kFragSPIRV)).setPCode(kFragSPIRV));

	vk::PipelineShaderStageCreateInfo vertStage;
	vertStage.stage = vk::ShaderStageFlagBits::eVertex;
	vertStage.module = vertModule.get();
	vertStage.pName = "main";

	vk::PipelineShaderStageCreateInfo fragStage;
	fragStage.stage = vk::ShaderStageFlagBits::eFragment;
	fragStage.module = fragModule.get();
	fragStage.pName = "main";

	// 两个书签组成管线最终使用的着色器清单
	std::vector<vk::PipelineShaderStageCreateInfo> stages = {vertStage, fragStage};

	// 7b. Vertex input: 三角形无 vertex buffer，所有位置在 VS 里硬编码
	vk::PipelineVertexInputStateCreateInfo vertexInput;

	// 7c. Input assembly
	vk::PipelineInputAssemblyStateCreateInfo inputAssembly;
	inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;

	// 7d. Viewport + Scissor: 设为 dynamic，运行时通过 cmd 设置
	vk::PipelineViewportStateCreateInfo viewportState;
	viewportState.viewportCount = 1;
	viewportState.scissorCount = 1;

	// 7e. Rasterizer
	vk::PipelineRasterizationStateCreateInfo rasterizer;
	rasterizer.polygonMode = vk::PolygonMode::eFill;
	rasterizer.lineWidth = 1.0f;
	rasterizer.cullMode = vk::CullModeFlagBits::eNone; // 三角形不裁剪
	rasterizer.frontFace = vk::FrontFace::eClockwise;

	// 7f. Multisampling: 不开
	vk::PipelineMultisampleStateCreateInfo multisampling;
	multisampling.rasterizationSamples = vk::SampleCountFlagBits::e1;

	// 7g. Color blending: 直接覆盖，无 alpha blending
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

	// 7i. m_PipelineLayout: 空 — 三角形不要 descriptor
	vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo;
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
	// 8. Framebuffers —— 给蓝图"填上具体的画布"
	// RenderPass 只说"我需要 1 个颜色附件"，Framebuffer 才真正指定"这个附件
	// 就是第 N 张 SwapChain 图像"。每个 SwapChain image 配一个，录制命令时
	// 按 image 下标挑对应的用。
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
	// 9. CommandPool + CommandBuffer —— 先录好"磁带"再一次性播放
	// Vulkan 是录制-提交模型：你不能直接下令画图，得先把所有指令录进
	// CommandBuffer，再整段 submit 到队列让 GPU 执行。CommandPool 就是这些
	// 磁带的内存分配池（按队列族划分）。这里建池并领一条磁带。
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
	// 10. 两个 Semaphore —— GPU 各阶段之间的"红绿灯"
	// 它们活在 GPU 内部，CPU 没法直接等。imageAvailable 表示"这张图你可以开画了"，
	// renderFinished 表示"我画完了可以去显示了"。Submit 时把它们串起来：
	// 等 imageAvailable 才动手，画完点亮 renderFinished，Present 再等它。
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
// RecordCommandBuffer — 录制一帧的全部绘制命令
// =========================================================================
void VulkanRenderer::RecordCommandBuffer(uint32_t imageIndex)
{
	m_CommandBuffer.reset();

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;

	m_CommandBuffer.begin(beginInfo);

	// Clear color: 深蓝黑背景
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

	// --- Draw: 3 个顶点，1 个实例，vertex buffer 不用 ---
	m_CommandBuffer.draw(3, 1, 0, 0);

	// --- End ---
	m_CommandBuffer.endRenderPass();
	m_CommandBuffer.end();
}

// =========================================================================
// Render — 每帧调用一次
//
// 5 步流水线，Semaphore 串起 GPU 内部同步：
//   Acquire → Record → Submit → Present → waitIdle（临时）
//
// Semaphore 是 GPU-GPU 信号：CPU 不阻塞，只告诉 GPU "等那个信号亮了再干活"。
// 这和 Fence（CPU-GPU 信号）不同——Fence 能让 CPU 等 GPU，Semaphore 不能。
// =========================================================================
void VulkanRenderer::Render()
{
	// 1. Acquire — 向 SwapChain 要一张可画的 Image。
	//    这张 Image 可能还在被显示器用（上一帧），驱动会阻塞直到有空闲的。
	//    拿到后 GPU 在 m_ImageAvailableSemaphore 上发信号。
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

	// 3. Submit — 把录好的 CommandBuffer 交给 GPU 队列执行。
	//    等 m_ImageAvailableSemaphore 亮了（Image 可用）才开始画；
	//    画完后点亮 m_RenderFinishedSemaphore（告诉显示器可以翻了）。
	//    pWaitDstStageMask 指定"在管线的哪个阶段等这个信号"——
	//    这里选 ColorAttachmentOutput 是因为我们只需要颜色输出，
	//    不需要等顶点/片元阶段（那些阶段不涉及 swapchain image 的读写）。
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

	// 4. Present — 等 GPU 画完（m_RenderFinishedSemaphore 亮）后翻到屏幕
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

	// 5. Wait: 简单同步——等 GPU 全部完成再继续下一帧
	//    后续 Phase 会升级为 Fence + 多帧并行
	m_Device->waitIdle();
}

// =========================================================================
// Shutdown — 显式逆序销毁资源
// =========================================================================
void VulkanRenderer::Shutdown()
{
	if (!m_Device)
		return;

	// 等 GPU 完成所有工作
	m_Device->waitIdle();

	// 逆序显式释放（UniqueHandle 析构时会自动释放，显式调用便于调试）
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
