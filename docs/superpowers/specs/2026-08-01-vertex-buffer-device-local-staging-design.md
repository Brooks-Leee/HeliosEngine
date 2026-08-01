# Vertex Buffer 升级：device-local + staging 上传 — 设计文档

> Phase 1.2.x | 2026-08-01

## 背景与目标

现有 vertex buffer（`VulkanRenderer.cpp:371-387`）用 **host-visible + host-coherent** 内存 + map/memcpy 上传。这在独立 GPU 上意味着顶点数据落在**系统 RAM**（PCIe BAR 窗口），GPU 每次 draw 都从系统 RAM 走 PCIe 抓顶点——"零拷贝"，但读取走慢路径。

目标：把顶点数据真正放进 **device-local（VRAM）**，用 staging buffer + `vkCmdCopyBuffer` 做一次 GPU DMA 搬运。**画面不变 = 改对**。

> 概念动机：静态几何（几乎不变）该住显存，用一次性 copy 换每帧全速读取；host-visible 零拷贝留给每帧变的数据（UBO、粒子）。这对应笔记 2.1 的资源更新频率分层。

## 设计决策

- **方案 A：内联一次性上传**。上传逻辑摊开写在 `Initialize` 里，延续单类单文件风格；不抽 helper。理由：
  - 这是第一次见 "staging → copy" 模式，摊开写 + 配笔记最利于消化
  - 时机学：MVP UBO（下一次任务）是同一模式的第二实例，那时再抽 `CopyBufferData()` 是"见两次才抽象"
- **上传走 graphics queue**，不引入独立 transfer queue family。独立 transfer queue 是资源量上来之后的优化（YAGNI）。
- **时序用 waitIdle 保证**：copy 提交后 `waitIdle()` 再返回。init 阶段可接受（反正每帧都 waitIdle）。因此不需要显式 barrier——waitIdle 本身就是全屏障。
- **staging 用 host-visible + coherent**：map/memcpy/unmap 姿势不变（沿用现有模式）。
- **临时 command buffer 用完即释放**，不长期占用 pool。
- 目标网格、pipeline、vertex input、`RecordCommandBuffer`、draw 调用**全部不动**。

## 改动点

改动限 `src/core/Render/Vulkan/VulkanRenderer.cpp` 的 vertex buffer 相关代码。**资源创建**（`:371-387`）与**数据上传**（insert 到 step 9 之后）是两处。

| # | 位置 | 改动 |
|---|---|---|
| 1 | `:373-374` | vertex buffer usage **加 `eTransferDst`**（保留 `eVertexBuffer`） |
| 2 | `:378-380` | 内存类型 `eHostVisible \| eHostCoherent` → **`eDeviceLocal`** |
| 3 | `:383-386` | **删除** map/memcpy/unmap 直写（创建块不再上传） |
| 4 | step 9（`:718-722`）之后 | **插入 staging 上传段**（见下） |

**为什么上传段放在 step 9 之后**：上传需要 command buffer（从 command pool 分配）提交到 graphics queue，而 command pool 在 step 9 才创建。资源"创建"（5g）与"上传"（init 末尾）分离，是因为上传依赖 command 机制——这个依赖顺序本身是教学点（创建 buffer 不需要命令机制，填充它才需要）。

### 改动点 1：buffer usage 声明（关键教学点）

buffer 的用途**创建时声明，不是运行时才知道**。`eTransferDst` 缺失时 validation 层报 `VUID-vkCmdCopyBuffer-dstBuffer-00119`（目标 buffer 必须有 transfer-dst usage）。同样，staging buffer 必须有 `eTransferSrc`（VUID-vkCmdCopyBuffer-srcBuffer-00118）。

### 改动点 2：内存类型

`FindMemoryType(memReq.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal)`——顶点终态住 VRAM，GPU 读取最快。

### 改动点 3+4：staging 上传段（删除直写，插入 step 9 之后）

```
// staging 是局部变量，不进成员——用完 RAII 自动清
vk::UniqueBuffer stagingBuffer = createBuffer(size, usage = eTransferSrc);
stagingBufferMemory = allocateMemory(eHostVisible | eHostCoherent);
bindBufferMemory(stagingBuffer, stagingBufferMemory, 0);
map → memcpy(g_TriangleVertices) → unmap

// 临时 command buffer（从 m_CommandPool 分配，eOneTimeSubmit）
begin
copyBuffer(stagingBuffer, m_VertexBuffer, {srcOffset=0, dstOffset=0, size})
end

// 提交到 graphics queue → waitIdle 保证 copy 完成
m_GraphicsQueue.submit(submitInfo, nullptr);
m_Device->waitIdle();

// 释放临时 command buffer + staging（RAII）
```

### 数据流（前后对比）

```
之前（host-visible，零拷贝但慢读）:
CPU: map → memcpy → unmap ──▶ host-visible buffer (= 系统 RAM) ──▶ GPU 走 PCIe 读

之后（device-local + staging，一次 copy 换全速读）:
CPU: map → memcpy ──▶ staging (host-visible)
staging ──vkCmdCopyBuffer (GPU DMA)──▶ vertex buffer (device-local VRAM) ──▶ GPU 全速读
```

### 错误处理与边界

- staging 创建 / 内存分配失败 → `std::runtime_error`（与现有风格一致，`:88`）
- 内存分配：staging 与 device-local 分别 `getBufferMemoryRequirements()`（两者 memory type 可能不同）
- 成员声明顺序不变：`m_VertexBufferMemory` 在 `m_VertexBuffer` 之前声明，保证 buffer 先于 backing memory 销毁（`:104-105` 注释）

## DX12 对照（写入笔记）

| Vulkan（本次实现） | DX12 | 本质 |
|---|---|---|
| device-local buffer | Default Heap | 数据终态住显存 |
| host-visible staging buffer | Upload Heap | 数据中转站（CPU 可写） |
| `vkCmdCopyBuffer` | `CopyBufferRegion` | GPU DMA 搬运 |

## 验证标准

- [ ] `build.bat` 编译，零 warning
- [ ] 改动文件跑 `clang-format -i`
- [ ] 运行：**画面不变**（5 个反色三角形）——几何/颜色/变换全没变，只是数据住址变了
- [ ] validation 层无新报错（尤其无 copy VU 报错）

## 文档产物（docs 提交需用户同意后单独做）

- 更新 `docs/notes/1.9-vertex-buffer-and-upload.md`：
  - map/memcpy 标注为"最简版（已被生产版替换）"
  - 新增一节：device-local + staging + copy 流程 + DX12 对照（Default Heap / Upload Heap / CopyBufferRegion）

## 后续（本次不做）

- **MVP 矩阵**：把 offset/scale 升级为 uniform buffer——届时抽 `CopyBufferData()` helper（同一模式的第二实例）
- 独立 transfer queue family
