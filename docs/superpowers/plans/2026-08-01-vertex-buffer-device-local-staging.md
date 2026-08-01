# Vertex Buffer device-local + staging 上传 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 把 vertex buffer 从 host-visible + map/memcpy 升级为 device-local（VRAM）+ staging buffer + `vkCmdCopyBuffer` 一次性 DMA 上传，画面不变。

**Architecture:** 改动全部在 `VulkanRenderer.cpp` 的 `Initialize()` 内。资源创建（`:371-387`）改为 device-local + 加 `eTransferDst`，删除 CPU 直写；在 step 9（command pool 创建）之后插入 staging 上传段——staging（host-visible）填数据 → 录一次性 command buffer 做 copy → submit → waitIdle → 释放临时资源。pipeline / vertex input / `RecordCommandBuffer` / draw 全部不动。

**Tech Stack:** C++20、Vulkan 1.4（vulkan.hpp `vk::UniqueHandle`）、CMake 3.25+、MSVC 2022。

## Global Constraints

- **画面不变 = 改对**：5 个反色三角形，几何/颜色/变换全不变
- 上传走 graphics queue，不引入独立 transfer queue family
- 时序用 `waitIdle` 保证；不需要显式 barrier
- staging 用 host-visible + coherent（沿用 map/memcpy/unmap 姿势）
- 临时 command buffer 用完即释放（`freeCommandBuffers`），staging 用局部 RAII 变量
- 目标网格、pipeline、vertex input、`RecordCommandBuffer`、draw 调用**全部不动**
- 编译零 warning；改动文件跑 `clang-format -i`；注释用英文，只解释 WHY
- 局部变量销毁顺序：staging memory 声明在 staging buffer 之前（buffer 先于 backing memory 销毁）
- **git 操作先问用户**（项目记忆规则：所有 git 操作都要先确认）

---

### Task 1: VulkanRenderer.cpp 内改动 — device-local 创建 + staging 上传段

**Files:**
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp:366-387`（5g vertex buffer 创建块）
- Modify: `src/core/Render/Vulkan/VulkanRenderer.cpp:733`（step 9 之后插入 5g-cont 上传段）

**Interfaces:**
- Consumes: 已有成员 `m_Device`、`m_CommandPool`、`m_GraphicsQueue`；已有 helper `FindMemoryType`；已有数据 `g_TriangleVertices`、`vertexBufferSize`；header 已 include `<vector>`、`<cstring>`（memcpy）
- Produces: 无新公开接口。`m_VertexBuffer` / `m_VertexBufferMemory` 语义不变（仍被 `RecordCommandBuffer` 绑定使用），只是数据上传方式变了

- [ ] **Step 1: 改写 5g 块——usage 加 eTransferDst、内存改 eDeviceLocal、删 CPU 直写**

把 `src/core/Render/Vulkan/VulkanRenderer.cpp:366-387` 整块（从 `// 5g. Vertex buffer...` 注释到 `std::cout << "[Vulkan] Vertex buffer created and uploaded.\n";`）替换为：

```cpp
	// =====================================================================
	// 5g. Vertex buffer — device-local (VRAM) memory, the fast path for the GPU to
	// read. Creation here is only the resource declaration; the data upload happens
	// in "5g-cont" below, after the command pool exists (a staging copy needs one).
	// =====================================================================
	vk::DeviceSize vertexBufferSize = sizeof(g_TriangleVertices[0]) * g_TriangleVertices.size();

	// eTransferDst: this buffer is the *destination* of the staging copy. Buffer
	// usages are declared at creation, not discovered at runtime — validation layer
	// rejects a copy into a buffer that wasn't born with eTransferDst.
	m_VertexBuffer = m_Device->createBufferUnique(vk::BufferCreateInfo{
		{}, vertexBufferSize, vk::BufferUsageFlagBits::eVertexBuffer | vk::BufferUsageFlagBits::eTransferDst,
		vk::SharingMode::eExclusive});

	vk::MemoryRequirements vbMemReq = m_Device->getBufferMemoryRequirements(m_VertexBuffer.get());
	m_VertexBufferMemory = m_Device->allocateMemoryUnique(vk::MemoryAllocateInfo{
		vbMemReq.size, FindMemoryType(vbMemReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)});
	m_Device->bindBufferMemory(m_VertexBuffer.get(), m_VertexBufferMemory.get(), 0);
	std::cout << "[Vulkan] Vertex buffer created (device-local; upload staged below).\n";
```

要点：`FindMemoryType` 第二参从 `eHostVisible | eHostCoherent` 改成 `eDeviceLocal`；usage 加 `| vk::BufferUsageFlagBits::eTransferDst`；删掉 map/memcpy/unmap 三行（device-local 内存 CPU 不能写）。

- [ ] **Step 2: 在 step 9 之后插入 5g-cont staging 上传段**

在 `std::cout << "[Vulkan] Command pool + buffer ready.\n";` 这一行之后、step 10（两个 semaphore）注释块之前，插入：

```cpp
	// =====================================================================
	// 5g-cont. Vertex data upload — device-local memory can't be written by the CPU.
	// Stage the data in a host-visible buffer (CPU can write it), then let the GPU
	// copy it into the device-local vertex buffer with a one-time DMA transfer.
	// Why not host-visible directly? The GPU reads host-visible memory over PCIe on
	// every draw; device-local is full-speed VRAM. One copy now buys fast reads forever.
	// =====================================================================
	vk::UniqueDeviceMemory stagingBufferMemory; // declared before buffer: buffer frees first
	vk::UniqueBuffer stagingBuffer = m_Device->createBufferUnique(vk::BufferCreateInfo{
		{}, vertexBufferSize, vk::BufferUsageFlagBits::eTransferSrc, vk::SharingMode::eExclusive});

	vk::MemoryRequirements stagingMemReq = m_Device->getBufferMemoryRequirements(stagingBuffer.get());
	stagingBufferMemory = m_Device->allocateMemoryUnique(vk::MemoryAllocateInfo{
		stagingMemReq.size, FindMemoryType(stagingMemReq.memoryTypeBits,
										   vk::MemoryPropertyFlagBits::eHostVisible |
											   vk::MemoryPropertyFlagBits::eHostCoherent)});
	m_Device->bindBufferMemory(stagingBuffer.get(), stagingBufferMemory.get(), 0);

	void* stagingData = m_Device->mapMemory(stagingBufferMemory.get(), 0, vertexBufferSize);
	std::memcpy(stagingData, g_TriangleVertices.data(), static_cast<size_t>(vertexBufferSize));
	m_Device->unmapMemory(stagingBufferMemory.get());

	// One-shot copy: record a command buffer, submit, wait for the GPU to finish.
	vk::CommandBufferAllocateInfo CopyAllocInfo;
	CopyAllocInfo.commandPool = m_CommandPool.get();
	CopyAllocInfo.level = vk::CommandBufferLevel::ePrimary;
	CopyAllocInfo.commandBufferCount = 1;
	std::vector<vk::CommandBuffer> copyBuffers = m_Device->allocateCommandBuffers(CopyAllocInfo);
	vk::CommandBuffer copyCmd = copyBuffers[0];

	vk::CommandBufferBeginInfo CopyBeginInfo;
	CopyBeginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	copyCmd.begin(CopyBeginInfo);

	vk::BufferCopy copyRegion{0, 0, vertexBufferSize}; // srcOffset, dstOffset, size
	copyCmd.copyBuffer(stagingBuffer.get(), m_VertexBuffer.get(), 1, &copyRegion);
	copyCmd.end();

	vk::SubmitInfo CopySubmitInfo;
	CopySubmitInfo.commandBufferCount = 1;
	CopySubmitInfo.pCommandBuffers = &copyCmd;
	m_GraphicsQueue.submit(CopySubmitInfo, nullptr);
	m_Device->waitIdle(); // GPU finished the copy; staging is safe to free

	m_Device->freeCommandBuffers(m_CommandPool.get(), 1, &copyCmd);
	std::cout << "[Vulkan] Vertex buffer uploaded via staging copy.\n";
```

- [ ] **Step 3: 格式化**

Run: `clang-format -i src/core/Render/Vulkan/VulkanRenderer.cpp`
Expected: 无输出；文件格式统一（Tab 缩进、Allman）。

- [ ] **Step 4: 编译**

Run: 项目根目录执行 `./build.bat`（Git Bash 下若直接执行 .bat 失败，用 `cmd //c build.bat`）
Expected: 编译成功，**零 warning**。若报错：先检查 `copyCmd.copyBuffer` 参数签名（`copyBuffer(src, dst, regionCount, pRegions)`）与 `vk::BufferCopy{srcOffset, dstOffset, size}` 聚合初始化。

- [ ] **Step 5: 运行验证**

Run: 运行 Debug 构建产物（路径约 `build/.../HeliosEngine.exe`，用 `ls build` 定位确切路径）
Expected: 窗口显示**与原画面一致的 5 个反色三角形**；console 无 validation ERROR/WARNING（尤其不应出现 `VUID-vkCmdCopyBuffer-dstBuffer-00119` 或 src 对应 VU——说明 usage 标志正确）；日志出现 `Vertex buffer created (device-local; upload staged below)` 和 `Vertex buffer uploaded via staging copy` 两行。

- [ ] **Step 6: 提交（先问用户）**

按项目记忆规则，commit 前向用户确认。确认后：

```bash
git add src/core/Render/Vulkan/VulkanRenderer.cpp
git commit -m "feat: upgrade vertex buffer to device-local memory with staging copy upload"
```

---

### Task 2: 更新学习笔记 1.9（docs，提交需用户同意后单独做）

**Files:**
- Modify: `docs/notes/1.9-vertex-buffer-and-upload.md`

**Interfaces:**
- Consumes: 无（纯文档）。引用的代码行为已在 Task 1 落地
- Produces: 更新后的笔记，供后续 MVP UBO 任务参考同一 staging 模式

- [ ] **Step 1: 把现有 map/memcpy 章节标记为"最简版（已被生产版替换）"**

在 `1.9-vertex-buffer-and-upload.md` 的 `## 上传四步（加 buffer/memory 三步，见 1.10）` 一节标题下加一行警示，并把 `g_TriangleVertices` 上传那段 code block 前加注：

```markdown
> ⚠️ **2026-08-01 更新：本节描述的是最简版（host-visible + map/memcpy 直写），已被生产版（device-local + staging copy）替换。** 保留作为概念铺垫：它是"最简单能跑的姿势"，但独立 GPU 上 GPU 读取要走 PCIe 慢路径。见下方新章节。
```

- [ ] **Step 2: 新增一节 "生产版：device-local + staging copy"**

在笔记末尾（`## 一句话记住` 之前）插入：

```markdown
## 生产版：device-local + staging copy（2026-08-01 起）

顶点终态住显存（VRAM），CPU 先写一块 host-visible 中转 buffer（staging），再让 GPU 用 `vkCmdCopyBuffer` 做一次性 DMA 搬运。

```
CPU: map → memcpy ──▶ staging (host-visible)
staging ──vkCmdCopyBuffer (GPU DMA)──▶ vertex buffer (device-local VRAM)
```

为什么绕这一圈：host-visible 内存 = 系统 RAM（PCIe BAR），GPU 每次 draw 都走 PCIe 读它——零拷贝但慢；device-local 是 VRAM，GPU 全速读。一次 copy 换一辈子的快速读取，摊薄后赚。这正是"资源更新频率分层"（见 2.1）：**静态几何住显存，每帧变的数据（UBO/粒子）留 host-visible**。

关键点：
- **buffer usage 创建时声明**：目标 buffer 要 `eVertexBuffer | eTransferDst`，staging 要 `eTransferSrc`。少了 validation 层报 `VUID-vkCmdCopyBuffer-dstBuffer-00119`（dst）/`-srcBuffer-00118`（src）。
- **上传需要 command buffer**：所以上传段放在 command pool 创建之后。资源"创建"与"数据填充"分离，后者依赖命令机制。
- **时序**：一次性 copy 用 `eOneTimeSubmit` command buffer，submit 后 `waitIdle` 保证完成再释放 staging。
- **临时资源**：staging 用局部 RAII 变量出作用域自清；临时 command buffer 用完 `freeCommandBuffers`。

## Vulkan vs DX12

| Vulkan | DX12 | 本质 |
|---|---|---|
| device-local buffer | Default Heap | 数据终态住显存 |
| host-visible staging buffer | Upload Heap | 数据中转站（CPU 可写） |
| `vkCmdCopyBuffer` | `CopyBufferRegion` | GPU DMA 搬运 |

DX12 逼你选 heap 类型（Default = 显存 / Upload = 中转），Vulkan 让你用 memory property flags 自己挑。两者机制完全相同。
```

- [ ] **Step 3: 提交（等用户同意，消息 `docs: daily summary` 风格）**

按 CLAUDE.md 约定，docs 不随代码提，agent 汇总改完文档后等用户同意再单独提交。**本步不自动执行。**

---

## Self-Review 备注

- **Spec 覆盖**：改动点 1（usage eTransferDst）→ Task1 Step1；改动点 2（eDeviceLocal）→ Task1 Step1；改动点 3（删直写）→ Task1 Step1；改动点 4（staging 段插 step9 后）→ Task1 Step2。验证标准（build/clang-format/画面不变/validation 干净）→ Task1 Steps 3-5。文档产物 → Task2。✅ 全覆盖。
- **无占位符**：所有代码块完整可直接执行。
- **类型一致性**：`vertexBufferSize`（vk::DeviceSize）在 Step1 定义、Step2 使用；`FindMemoryType` 签名沿用现有（`(uint32_t, vk::MemoryPropertyFlags)`）；`copyCmd.copyBuffer` 参数序按 vulkan.hpp 签名。`vk::BufferCopy{0, 0, size}` 聚合初始化对应 `{srcOffset, dstOffset, size}`。✅
