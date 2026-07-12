# HeliosEngine Learning Roadmap

> 目标：每阶段吃透一个主题。不贪快，求彻底。

---

## Phase 1: 开天辟地 — Window + Hello Triangle

**学习目标**：理解 Windows 消息机制，理解 GPU 渲染管线的每一环节

### 1.1 Win32 窗口
- [ ] 注册窗口类 + 创建窗口 + 消息循环
- [ ] 理解 `GetMessage` vs `PeekMessage`（游戏为什么用 PeekMessage）
- [ ] 处理窗口 resize、DPI 变化

### 1.2 Vulkan Hello Triangle（← 先做这个）

> **设计决策**：先深入 Vulkan 的具体实现，不做 RHI 抽象。等 1.3 DX12 也写完后，
> 再回头提炼公共接口——对两个 API 都有第一手经验时，抽象才有含金量。

- [x] Instance / Physical Device / Logical Device
- [x] Surface + SwapChain
- [x] Render Pass + Framebuffer
- [x] Pipeline Layout + Descriptor Set Layout
- [x] Graphics Pipeline（对比 DX12 PSO 的差异）
- [x] Semaphore + Fence（对比 DX12 的同步模型）
- [x] **写出三角形**

### 1.2.x Vulkan Hello Triangle — 扩展（同一 Phase 内的深入练习）

> 核心三角形跑通后，仍在 Phase 1.2 范围内继续吃透 Vulkan 的"多物件 / 资源 / 多 Pass"。
> 这部分每一项都可以独立成一个带 `[1.2.x]` 标记的提交。

- [x] 多物件渲染（push constant 传 `offset/scale`，`RecordCommandBuffer` 循环 `draw()`，RenderPass/Framebuffer 不动）
- [x] Shader 改从 `.spv` 文件加载（CMake 注入 `HELIOS_SHADER_DIR`，去掉硬编码 SPIR-V 数组）
- [ ] Vertex Buffer + MVP 矩阵（真正的 mesh 变换写法，替代 `offset/scale`）
- [ ] **多 Pass（offscreen → post）**：引入 subpass 与多 render pass 的自然场景
- [ ] Descriptor Set 实战（把 MVP / 纹理从 push constant 升级到 descriptor）

### 1.3 DX12 Hello Triangle
- [ ] Device 创建（理解 Adapter、Feature Level）
- [ ] Command Queue / Command List / Command Allocator（理解 CPU-GPU 同步模型）
- [ ] SwapChain（理解 back buffer、present、vsync）
- [ ] Root Signature（理解资源绑定模型）
- [ ] PSO — Pipeline State Object（理解为什么 DX12 把状态做成不可变）
- [ ] Fence + 双缓冲同步
- [ ] **写出三角形**

### 1.4 总结：提炼 RHI + 设计哲学对比
- [ ] 基于 1.2 + 1.3 的经验，提炼 RHI 公共接口
- [ ] 写一篇笔记：DX12 vs Vulkan 设计哲学对比

---

## Phase 2: 内存与资源 — 变成 C++ 高手

**学习目标**：掌握 GPU 资源生命周期，理解现代 C++ 所有权模型

### 2.1 GPU 资源管理
- [ ] Buffer（Vertex / Index / Constant / Structured）
- [ ] Texture（2D / Cube / Mip chain）
- [ ] Upload Heap vs Default Heap（CPU-GPU 数据传输）
- [ ] Resource Barrier / Layout Transition

### 2.2 内存分配器
- [ ] 写一个最简单的 Buddy Allocator（理解分裂与合并）
- [ ] 写一个 Slab Allocator
- [ ] 实现 GPU 资源的 Move-Only 句柄（理解 std::unique_ptr 的设计逻辑）
- [ ] RAII 包装所有 GPU 资源

### 2.3 Descriptor 管理
- [ ] DX12：Descriptor Heap + 动态索引（为什么 DX12 这样做？）
- [ ] Vulkan：Descriptor Pool + Descriptor Set（为什么 Vulkan 这样做？）
- [ ] 对比两种策略的优劣（bindless 的动机是什么？）

---

## Phase 3: 多线程 — TA 的硬骨头

**学习目标**：理解现代 CPU 架构的并发模型

### 3.1 基础
- [ ] std::thread / std::jthread（不用 pthread_*）
- [ ] std::mutex / std::shared_mutex / std::atomic
- [ ] 理解 memory order：relaxed / acquire / release / seq_cst
- [ ] 写一个 SpinLock

### 3.2 无锁数据结构
- [ ] 手写 SPSC 无锁队列（理解 CAS loop）
- [ ] 手写 MPMC 无锁队列
- [ ] 对比有锁 vs 无锁的性能差异

### 3.3 Job System
- [ ] 设计 Job 结构（function + data）
- [ ] 实现线程池 + work stealing
- [ ] 实现简单的 Task Graph（dependency-based 调度）

### 3.4 并行渲染
- [ ] DX12 multi-threaded command list recording
- [ ] Vulkan secondary command buffers
- [ ] 对比：哪种模型的 overhead 更小？为什么？

---

## Phase 4: 渲染管线

**学习目标**：把材质思维反推成引擎设计

### 4.1 Shader 编译管线
- [ ] dxc 集成：HLSL → DXIL
- [ ] glslang/glslc 集成：GLSL → SPIR-V
- [ ] Shader 变体管理（permutation / specialization constant）
- [ ] 运行时 Shader 热重载

### 4.2 Material System
- [ ] 设计 Material 数据模型（你的强项，这里要反推引擎设计）
- [ ] Constant Buffer 管理（per-frame / per-object / per-material）
- [ ] Texture 绑定（slot vs bindless 的取舍）
- [ ] 材质参数序列化

### 4.3 Render Graph
- [ ] 理解 FrameGraph / RDG 的设计动机
- [ ] 实现一个简单的 Render Pass 调度器
- [ ] 自动 Barrier / Layout Transition 推导

### 4.4 PBR Pipeline
- [ ] Cook-Torrance BRDF
- [ ] IBL（Irradiance Map + Prefiltered Env Map + BRDF LUT）
- [ ] Shadow Mapping（PCF / Variance Shadow Map）
- [ ] SSAO
- [ ] Tone Mapping + HDR → LDR

---

## Phase 5: 进阶

**学习目标**：现代引擎的前沿技术

### 5.1 Bindless / GPU-driven
- [ ] Descriptor Indexing (DX12) / Descriptor Indexing (Vulkan)
- [ ] GPU-driven culling
- [ ] Mesh / Material ID → 一次 DrawIndirect

### 5.2 Ray Tracing
- [ ] DXR (DX12 Ray Tracing)
- [ ] VK_KHR_ray_tracing
- [ ] RTAO / RT Reflection / RT Shadow

### 5.3 工具
- [ ] PIX / RenderDoc 调优
- [ ] 性能分析（GPU timing, bandwidth profiling）
- [ ] 内存分析

### 5.4 Editor
- [ ] ImGui 集成
- [ ] Scene Hierarchy
- [ ] Property Editor
- [ ] Asset Browser

---

## 进度记录

| 日期 | 完成内容 | 笔记 |
|------|----------|------|
| 2026-06-22 | 项目初始化 | — |
| 2026-07-11 | Phase 1.1 Win32 窗口 | RAII 封装 HWND，PeekMessage 消息循环 |
| 2026-07-11 | Phase 1.2 Vulkan 开始 | 设计文档完成，开始实现 Vulkan Hello Triangle |
| 2026-07-13 | Phase 1.2 核心完成 + 扩展 | 写出三角形；多物件渲染（push constant）、.spv 加载；注释英化 |
