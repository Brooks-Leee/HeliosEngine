# MVP 矩阵：动态偏移 UBO + DirectXMath + 3D 相机 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** base pass 从 push constant（offset/scale）升级为真正的 MVP：每对象一个组合 MVP 矩阵放 dynamic-offset UBO（descriptor 实战），引入 DirectXMath + 3D 相机 + 透视投影，5 个三角形散布 3D 空间。

**Architecture:** shader 加 `layout(binding=0) uniform PerObject { mat4 mvp; }`；C++ 建 host-visible UBO（持久映射）+ descriptor 四件套（`eUniformBufferDynamic`）+ base pipeline layout 换成 descriptor set layout（删 push constant）；每帧用 DirectXMath 算 5 个 `M·V·P`（transpose 后 memcpy 进对应槽）；录制时每 draw 用 dynamic offset 选槽。

**Tech Stack:** C++20、Vulkan 1.4（vulkan.hpp）、DirectXMath（Windows SDK，NOMINMAX 已由 CMake 全局定义）、glslc。

## Global Constraints

- **5 个三角形以正确透视显示在 3D 空间，图像不上下颠倒（y-flip 生效）**
- **相机绕 Y 缓慢旋转**（= per-frame UBO 更新路径在跑）
- UBO：host-visible + coherent，init 持久映射一次，每帧 memcpy 更新；**无 staging、无 eTransferDst**（per-frame 数据频率分层教学点）
- descriptor：binding 0、`eUniformBufferDynamic`、stage `eVertex`；`DescriptorBufferInfo{ buffer, offset=0, range=uboStride }`
- base pipeline layout：**删 push constant range，加 descriptor set layout**（`TrianglePush` 结构体删除）
- RecordCommandBuffer：5×（`bindDescriptorSets(dynamicOffset=i·stride)` → `draw`），删除 `pushConstants` 调用
- shader 用 `glslc` 重编 `.spv`（`/c/VulkanSDK/1.4.350.0/Bin/glslc`）
- 编译零 warning；注释英文只讲 WHY；改动文件跑 `clang-format -i`（若本机无 clang-format 则手工核对 Tab/Allman）
- 画面标准从"不变"改为"透视/布局正确"（human 视觉确认）
- **git 操作先问用户**；docs 提交需用户同意后单独做

---

### Task 1: MVP 完整代码改动（shader + C++ + 重编 + 构建 + 运行验证）

**Files:**
- Modify: `shaders/vulkan/triangle.vert`（重写）
- Modify: `shaders/vulkan/triangle.vert.spv`（glslc 重编）
- Modify: `src/core/Render/Vulkan/VulkanRenderer.h`（加成员）
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp`（删 TrianglePush、加 UBO/descriptor、改 pipeline layout、改 RecordCommandBuffer、改 Render、改 Shutdown、加 DirectXMath include）

**Interfaces:**
- Consumes: 已有 `m_Device`、`m_PhysicalDevice`、`m_CommandPool`、`m_GraphicsQueue`、`FindMemoryType`、`m_VertexBuffer`/`m_VertexBufferMemory`（不动）、`m_PipelineLayout`（复用，内容变）、`m_SwapChainExtent`
- Produces: 新成员 `m_UniformBuffer`/`m_UniformBufferMemory`/`m_UniformBufferMapped`/`m_UniformBufferStride`/`m_DescriptorSetLayout`/`m_DescriptorPool`/`m_DescriptorSet`。无新公开接口

- [ ] **Step 1: 重写 triangle.vert**

把 `shaders/vulkan/triangle.vert` 整个文件替换为：

```glsl
#version 450

// Per-vertex inputs, fed from a real vertex buffer.
// location must match the VertexInputAttributeDescription set up on the C++ side.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

// Per-object MVP matrix, one slot per object in a dynamic-offset uniform buffer.
// The CPU picks the slot per draw via a dynamic offset (see RecordCommandBuffer).
// Replaces the old push_constant {offset, scale}: large per-object data goes
// through a descriptor, not a push constant (see note 1.8).
layout(binding = 0) uniform PerObject {
    mat4 mvp;
} ubo;

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = ubo.mvp * vec4(inPos, 0.0, 1.0);
    fragColor = inColor;
}
```

- [ ] **Step 2: 重编 SPIR-V**

Run: `/c/VulkanSDK/1.4.350.0/Bin/glslc shaders/vulkan/triangle.vert -o shaders/vulkan/triangle.vert.spv`
Expected: 无输出（成功）。确认 `triangle.vert.spv` 时间戳更新。

- [ ] **Step 3: 头文件加成员**

在 `src/core/Render/Vulkan/VulkanRenderer.h` 的 vertex buffer 成员块（`m_VertexBufferMemory` / `m_VertexBuffer`，约 `:104-105`）之后插入：

```cpp
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
```

- [ ] **Step 4: .cpp 顶部加 include**

`src/core/Render/Vulkan/VulkanRenderer.cpp` 的 include 区（`#include <windows.h>` 附近）加：

```cpp
#include <cmath>
#include <DirectXMath.h>
```

- [ ] **Step 5: 删 TrianglePush 结构体**

删除 `src/core/Render/Vulkan/VulkanRenderer.cpp` 中的 `struct TrianglePush { float offset[2]; float scale; };` 整块（含上方注释）。

- [ ] **Step 6: init 加 UBO + descriptor 四件套**

在 5g-cont staging 上传段之后、`// 6. RenderPass` 注释块之前插入：

```cpp
	// =====================================================================
	// 5h. Per-object MVP uniform buffer + descriptor.
	// Frequency-layering counterpart to the vertex buffer: geometry is static
	// (device-local, uploaded once), but the per-object matrices change every
	// frame (camera orbits), so the UBO lives in host-visible memory and is
	// rewritten in place each frame — no staging copy.
	// =====================================================================
	const uint32_t minAlign = m_PhysicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
	m_UniformBufferStride = 64; // sizeof(XMFLOAT4X4) = one mat4
	m_UniformBufferStride = (m_UniformBufferStride + minAlign - 1) / minAlign * minAlign;

	const vk::DeviceSize uboSize = 5 * m_UniformBufferStride;

	m_UniformBuffer = m_Device->createBufferUnique(vk::BufferCreateInfo{
		{}, uboSize, vk::BufferUsageFlagBits::eUniformBuffer, vk::SharingMode::eExclusive});

	vk::MemoryRequirements uboMemReq = m_Device->getBufferMemoryRequirements(m_UniformBuffer.get());
	m_UniformBufferMemory = m_Device->allocateMemoryUnique(vk::MemoryAllocateInfo{
		uboMemReq.size, FindMemoryType(uboMemReq.memoryTypeBits,
									   vk::MemoryPropertyFlagBits::eHostVisible |
										   vk::MemoryPropertyFlagBits::eHostCoherent)});
	m_Device->bindBufferMemory(m_UniformBuffer.get(), m_UniformBufferMemory.get(), 0);
	m_UniformBufferMapped = m_Device->mapMemory(m_UniformBufferMemory.get(), 0, uboSize);

	// Descriptor four-piece: layout (binding 0 = dynamic uniform buffer) → pool → set → write.
	vk::DescriptorSetLayoutBinding uboBinding;
	uboBinding.binding = 0;
	uboBinding.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
	uboBinding.descriptorCount = 1;
	uboBinding.stageFlags = vk::ShaderStageFlagBits::eVertex;
	m_DescriptorSetLayout = m_Device->createDescriptorSetLayoutUnique(vk::DescriptorSetLayoutCreateInfo{{}, 1, &uboBinding});

	vk::DescriptorPoolSize poolSize;
	poolSize.type = vk::DescriptorType::eUniformBufferDynamic;
	poolSize.descriptorCount = 1;
	m_DescriptorPool = m_Device->createDescriptorPoolUnique(vk::DescriptorPoolCreateInfo{{}, 1, 1, &poolSize});

	auto uboSets = m_Device->allocateDescriptorSetsUnique(
		vk::DescriptorSetAllocateInfo{m_DescriptorPool.get(), 1, &m_DescriptorSetLayout.get()});
	m_DescriptorSet = std::move(uboSets[0]);

	// The descriptor points at the whole UBO; the per-draw dynamic offset selects the slot.
	vk::DescriptorBufferInfo uboInfo;
	uboInfo.buffer = m_UniformBuffer.get();
	uboInfo.offset = 0;
	uboInfo.range = m_UniformBufferStride;
	vk::WriteDescriptorSet uboWrite;
	uboWrite.dstSet = m_DescriptorSet.get();
	uboWrite.dstBinding = 0;
	uboWrite.dstArrayElement = 0;
	uboWrite.descriptorCount = 1;
	uboWrite.descriptorType = vk::DescriptorType::eUniformBufferDynamic;
	uboWrite.pBufferInfo = &uboInfo;
	m_Device->updateDescriptorSets(1, &uboWrite, 0, nullptr);
	std::cout << "[Vulkan] MVP uniform buffer + descriptor ready.\n";
```

- [ ] **Step 7: base pipeline layout 换 descriptor、删 push constant**

把 base pipeline layout 创建块（当前含 `PushConstantRange pushRange` + `PipelineLayoutCreateInfo.pushConstantRangeCount=1` 那段）替换为：

```cpp
	vk::PipelineLayoutCreateInfo PipelineLayoutCreateInfo;
	PipelineLayoutCreateInfo.setLayoutCount = 1;
	PipelineLayoutCreateInfo.pSetLayouts = &m_DescriptorSetLayout.get();
	m_PipelineLayout = m_Device->createPipelineLayoutUnique(PipelineLayoutCreateInfo);
```

（`TrianglePush` 已删，pushRange 引用随之消失。post pipeline layout 不动。）

- [ ] **Step 8: RecordCommandBuffer 改 dynamic offset 循环**

把当前 5 对象循环（含 `const std::array<TrianglePush, 5> objects` 数组 + `pushConstants` + `draw`）整体替换为：

```cpp
	// Multi-object: same pipeline, draw N times; each iteration binds the descriptor
	// set at a different dynamic offset so the vertex shader reads a different MVP slot.
	for (uint32_t i = 0; i < 5; ++i)
	{
		uint32_t dynamicOffset = i * m_UniformBufferStride;
		m_CommandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_PipelineLayout.get(), 0, 1,
										   &m_DescriptorSet.get(), 1, &dynamicOffset);
		m_CommandBuffer.draw(3, 1, 0, 0);
	}
```

（`bindPipeline`、`setViewport`、`setScissor`、`bindVertexBuffers` 保持原样在其之前。）

- [ ] **Step 9: Render() 加相机 + 每帧 UBO 更新**

在 `VulkanRenderer::Render()` 开头（`acquireNextImageKHR` 之前）插入：

```cpp
	// =====================================================================
	// Per-frame MVP update — the camera orbits slowly, so the matrices change
	// every frame. The host-visible UBO is persistently mapped; just memcpy.
	// DirectXMath is row-major (row-vector order): model * view * proj, then
	// transpose to GLSL's column-major before storing. proj._22 *= -1 flips
	// D3D's y-up to Vulkan's y-down NDC.
	// =====================================================================
	{
		using namespace DirectX;
		const float timeSec = static_cast<float>(GetTickCount64()) / 1000.0f;
		const float aspect =
			static_cast<float>(m_SwapChainExtent.width) / static_cast<float>(m_SwapChainExtent.height);

		XMMATRIX view = XMMatrixLookAtRH(
			XMVectorSet(std::sin(timeSec * 0.3f) * 5.0f, 1.5f, std::cos(timeSec * 0.3f) * 5.0f, 0.0f),
			XMVectorSet(0.0f, 0.0f, 0.0f, 0.0f),
			XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f));
		XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60.0f), aspect, 0.1f, 100.0f);
		proj._22 *= -1.0f; // D3D y-up -> Vulkan y-down

		const XMFLOAT3 positions[5] = {
			{0.0f, 0.0f, 0.0f},
			{-1.5f, -1.0f, 2.0f},
			{1.5f, -1.0f, 2.0f},
			{-1.5f, 1.0f, -2.0f},
			{1.5f, 1.0f, -2.0f},
		};

		for (uint32_t i = 0; i < 5; ++i)
		{
			XMMATRIX model = XMMatrixTranslation(positions[i].x, positions[i].y, positions[i].z);
			XMFLOAT4X4 out;
			XMStoreFloat4x4(&out, XMMatrixTranspose(model * view * proj));
			std::memcpy(static_cast<char*>(m_UniformBufferMapped) + i * m_UniformBufferStride, &out, sizeof(out));
		}
	}
```

- [ ] **Step 10: Shutdown 清理新成员**

在 `VulkanRenderer::Shutdown()` 中，`m_PipelineLayout.reset();` 之后、`m_VertexBuffer.reset();` 之前插入：

```cpp
	m_DescriptorSet.reset();
	m_DescriptorPool.reset();
	m_DescriptorSetLayout.reset();
	if (m_UniformBufferMapped)
	{
		m_Device->unmapMemory(m_UniformBufferMemory.get());
		m_UniformBufferMapped = nullptr;
	}
	m_UniformBuffer.reset();
	m_UniformBufferMemory.reset();
```

- [ ] **Step 11: 格式化**

Run: `clang-format -i src/core/Render/Vulkan/VulkanRenderer.h src/core/Render/Vulkan/VulkanRenderer.cpp`
Expected: 无输出。若本机无 clang-format，手工核对 Tab 缩进、Allman、120 列。

- [ ] **Step 12: 编译**

Run: 项目根目录 `cmd //c "cd /d E:\\HeliosEngine && .\\build.bat"`
Expected: 编译成功，**零 warning**。若报错：
- DirectXMath 相关 min/max 冲突 → 已由 NOMINMAX 全局定义，不应出现
- `XMFLOAT3` / `XMMatrix*` 未识别 → 确认 Step 4 的 include 在位
- `proj._22` 访问器 → DirectXMath `XMMATRIX::_22` 是合法成员

- [ ] **Step 13: 运行验证**

Run: `./build/bin/Debug/HeliosEngine.exe` 后台跑几秒（`&` + `sleep 6` + `kill`），捕获输出。
Expected:
- 无 validation ERROR/WARNING（尤其无 pipeline/descriptor 相关 VU）
- 日志出现 `[Vulkan] MVP uniform buffer + descriptor ready.`，`[Vulkan] Initialization complete.`
- 进程稳定不崩

**human 视觉确认（implementer 完成后由用户跑 exe 看）**：
- 5 个三角形 3D 透视布局（近大远小）
- **图像不上下颠倒**（y-flip 生效）
- 相机绕 Y 缓慢旋转
- 重叠区有绘制顺序 artifact（预期）

- [ ] **Step 14: 提交（先问用户）**

按项目记忆规则，commit 前向用户确认。确认后：
```bash
git add shaders/vulkan/triangle.vert shaders/vulkan/triangle.vert.spv src/core/Render/Vulkan/VulkanRenderer.h src/core/Render/Vulkan/VulkanRenderer.cpp
git commit -m "feat: replace push-constant transforms with per-object MVP uniform buffer (dynamic offset) + 3D camera"
```

---

### Task 2: 学习笔记（docs，提交需用户同意后单独做）

**Files:**
- Modify: `docs/notes/1.8-descriptor-vs-pushconstant.md`

**Interfaces:**
- Consumes: Task 1 落地后的代码行为
- Produces: 更新后的笔记，解释 MVP + dynamic offset + transpose + y-flip + 频率分层对照

- [ ] **Step 1: 在笔记 1.8 末尾新增一节**

在 `## 一句话记住` 之前插入：

```markdown
## 实战：MVP 矩阵走 dynamic-offset UBO（2026-08-01 起）

base pass 的 per-object 变换从 push constant（offset/scale）升级为 uniform buffer。关键设计：

- **一个大 UBO，动态偏移选槽**：5 个对象各一个组合 MVP，descriptor 绑一次，逐 draw 用 `dynamicOffset = i·stride` 选第几个。对象再多也不用建多个 descriptor set——这就是"per-object 大参数为何走 descriptor"的答案。
- **槽要对齐**：每槽按 `minUniformBufferOffsetAlignment`（常 256B）对齐；`DescriptorBufferInfo.range = stride`，访问区 `[i·stride, (i+1)·stride)` 严格在 buffer 内。
- **频率分层对照**（见 2.1）：顶点是静态几何 → device-local + staging 一次上传；MVP 每帧变 → host-visible 持久映射每帧 memcpy，**无 staging**。
- **DirectXMath ↔ GLSL 两个坑**：
  - row/col-major：DirectXMath row-major（行向量 `v·M`），GLSL column-major（列向量 `M·v`）。写 UBO 前 `XMMatrixTranspose`。这是 D3D 数学 ↔ column-major shader 的经典坑。
  - y 轴：D3D NDC y-up，Vulkan y-down。`proj._22 *= -1` 翻转（转置不影响对角线）。
  - z∈[0,1] 两边一致，无额外处理。

| Vulkan | DX12 |
|---|---|
| uniform buffer + dynamic offset | CBV + descriptor heap 偏移（一个 heap 多个 CBV） |
| `eUniformBufferDynamic` | D3D12_DESCRIPTOR_HEAP_TYPE_CBV + offsetting |
```

- [ ] **Step 2: 提交（等用户同意，`docs: daily summary` 风格）**

按 CLAUDE.md 约定，docs 不随代码提，等用户同意后单独提交。**本步不自动执行。**

---

## Self-Review 备注

- **Spec 覆盖**：改动点 1（shader）→ Task1 Step1-2；改动点 2（UBO 创建/对齐）→ Task1 Step6；改动点 3（descriptor 四件套）→ Step6；改动点 4（pipeline layout）→ Step7；改动点 5（相机 + 每帧更新）→ Step9；改动点 6（RecordCommandBuffer）→ Step8；改动点 7（include/清理）→ Step4/10。验证标准 → Step12-13。文档产物 → Task2。✅
- **无占位符**：所有代码完整可直接转写。
- **类型一致性**：`m_UniformBufferStride` 在 Step6 计算、Step8/9 使用；`m_DescriptorSetLayout` Step6 创建、Step7 引用；`m_UniformBufferMapped` Step6 赋值、Step9/10 使用。DirectXMath 签名核对（`XMMatrixLookAtRH/Translation/Transpose/PerspectiveFovRH`、`XMStoreFloat4x4`、`XMFLOAT3`）。`proj._22` 是 XMMATRIX 合法访问器。✅
- **约束一致性**：`NOMINMAX` 已由 CMake 全局定义（CMakeLists.txt:29），DirectXMath 可直接 include；post pipeline/descriptor 不动；`RecordCommandBuffer` 的 bindPipeline/viewport/scissor/vertex buffer 不动。
