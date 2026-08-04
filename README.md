# ☀️ HeliosEngine 曙光引擎

> 从零手写、用于学习的游戏引擎 —— DX12 + Vulkan，C++20，Windows。

[![C++](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus)](https://isocpp.org/)
[![Vulkan](https://img.shields.io/badge/Vulkan-1.3-AC162C?logo=vulkan)](https://www.vulkan.org/)
[![DirectX](https://img.shields.io/badge/DirectX-12-0078D4?logo=directx)](https://learn.microsoft.com/windows/win32/direct3d12/direct3d-12-graphics)
[![CMake](https://img.shields.io/badge/CMake-3.25%2B-064F8C?logo=cmake)](https://cmake.org/)
[![Windows](https://img.shields.io/badge/Windows-11-0078D6?logo=windows)](https://www.microsoft.com/windows)

---

## 📖 项目简介

HeliosEngine 是一个 **从零手写** 的游戏引擎，用于补齐引擎底层知识——图形 API、
内存模型、构建管线、现代 C++ 工程实践，每一块都要亲手摸过。

**设计哲学：**

- **不做 RHI 抽象，先吃透具体 API** —— 先用第一手经验走完 Vulkan 与 DX12，再回头提炼公共接口，抽象才有含金量
- **每阶段只吃透一个主题**，不贪快、求彻底（见 [`docs/roadmap.md`](docs/roadmap.md)）
- **手写一切** —— Win32 窗口与消息循环（无 GLFW / SDL），数学库规划自研（SIMD）
- **为什么 > 是什么** —— 代码注释只解释 WHY，概念本质沉淀进学习笔记

## 🤖 Agent 协作

本项目从设计上支持 **AI agent 直接参与开发**——写代码、跑构建、查 bug、改文档，都可以直接指派给 agent（如 Claude Code）完成。

- **上手即用**：仓库根的 [`CLAUDE.md`](CLAUDE.md) 定义了完整的协作规则（角色定位、编码规范、提交纪律），agent 开工前自动加载——说一句「帮我实现 XXX / 修复这个 bug」，它就能按项目规范直接开工
- **多人 / 多 agent 并行**：每日进展记录在 [`docs/sessions/`](docs/sessions/)（只追加不覆盖，每个块带 `[agent:xxx]` 身份头）；当前焦点与入口文件见 [`docs/STATUS.md`](docs/STATUS.md) 活指针
- **提交纪律**：代码可随时提交；`docs/` 文档由人工确认后再提
- **质量门槛**：编译零 warning、`.clang-format` 统一格式、注释只写 WHY

## 🛠 技术栈

| 项目 | 选型 |
|------|------|
| 语言 | C++20（MSVC） |
| 构建 | CMake 3.25+ |
| 图形 API | DirectX 12 + Vulkan 1.3 |
| Shader 编译器 | glslc（SPIR-V）/ dxc（DXIL） |
| 窗口系统 | Win32 API（手写消息循环） |
| 数学库 | 自研（规划中） |

## 📂 项目结构

```
HeliosEngine/
├── src/
│   ├── main.cpp                      # 程序入口（窗口 + 主循环）
│   └── core/
│       ├── Platform/
│       │   └── Window.h / Window.cpp # Win32 RAII 窗口封装
│       ├── Render/
│       │   └── Vulkan/
│       │       └── VulkanRenderer    # Vulkan 渲染器（全实现，含 WHY 注释）
│       └── Math/                     # 自研数学库（规划中）
├── shaders/
│   └── vulkan/                       # GLSL 源，构建时由 glslc 自动编译为 .spv
│       ├── triangle.vert / .frag     # 三角形 + MVP 相机变换
│       ├── post.vert / post.frag     # 多 Pass 实验
│       └── experiments/              # 实验 shader 归档
├── assets/                           # 资源目录（预留）
├── docs/
│   ├── roadmap.md                    # 学习路线图与进度追踪
│   ├── STATUS.md                     # 当前状态活指针（每次会话更新）
│   ├── notes/                        # 学习笔记（16 篇）
│   └── sessions/                     # 会话日志（checkpoint）
├── scripts/                          # 辅助脚本
├── build.bat                         # 一键构建（VS2022）
├── CMakeLists.txt                    # 顶层构建配置
└── .clang-format                     # 编码风格（UE 风）
```

## 🔨 如何构建

### 前置要求

- **Windows 10/11**
- **Visual Studio 2022**（含「使用 C++ 的桌面开发」工作负载）
- **Vulkan SDK 1.3+** —— 构建时需要 `glslc`（Vulkan SDK 自带，需在 PATH 或 `VULKAN_SDK` 中）

### 一键构建

```bat
build.bat
```

（内部依次执行 `vcvarsall.bat x64` → `cmake -B build -S .` → `cmake --build build`）

### 手动构建

```bat
cmake -B build -S .
cmake --build build --config Debug
```

### 产物位置

```
build/bin/Debug/HeliosEngine.exe    （Debug）
build/bin/Release/HeliosEngine.exe  （Release）
```

### CMake 选项

| 选项 | 默认 | 说明 |
|------|------|------|
| `HELIOS_BUILD_DX12` | ON | DirectX 12 后端（Phase 1.3 实现） |
| `HELIOS_BUILD_VULKAN` | ON | Vulkan 后端（当前唯一可用后端） |
| `HELIOS_BUILD_EDITOR` | OFF | 编辑器（未来） |
| `HELIOS_BUILD_TESTS` | OFF | 单元测试 |

### Shader 编译

`shaders/vulkan/` 下的 `.vert` / `.frag` 由 CMake 自动调用 `glslc` 编译成 `.spv`。
`.spv` 是构建产物（已 gitignore）——**改完 GLSL 直接重新 build 即可**，无需手动编译。

## 🏃 运行

构建完成后运行 exe，会打开窗口渲染一个三角形（MVP 矩阵 + 3D 相机 + 顶点色），
Vulkan validation layer 默认开启，便于学习阶段排查错误。

## 🗺 当前进度

| 阶段 | 内容 | 状态 |
|------|------|------|
| 1.1 | Win32 窗口 + 消息循环 | ✅ |
| 1.2 | Vulkan Hello Triangle（Instance → Pipeline → 同步 → 上屏） | ✅ |
| 1.2.x | 多物件（push constant）、`.spv` 加载、Vertex Buffer（device-local + staging） | ✅ |
| 1.2.x | MVP 矩阵（dynamic-offset UBO + DirectXMath 3D 相机 + 透视 y-flip） | ✅ |
| 1.2.x | 多 Pass（offscreen → post）、Depth Attachment | ⏳ 下一步 |
| 1.3 | DX12 Hello Triangle | ⏳ |
| 1.4 | RHI 提炼 + DX12 vs Vulkan 设计哲学对比 | ⏳ |
| 2.x | 内存与资源管理 | ⏳ |

## 📚 学习笔记

对话中聊透的概念顺手沉淀成文，每个文件聚焦一个主题，持续更新。
完整索引见 [`docs/notes/README.md`](docs/notes/README.md)。

### Phase 1：Vulkan 基础

| 笔记 | 一句话 |
|------|--------|
| [RAII 与资源管理](docs/notes/1.1-raii-and-resource-management.md) | 资源生命周期绑在对象构造/析构上 |
| [Vulkan API 绑定](docs/notes/1.2-vulkan-api-bindings.md) | C API vs vulkan.hpp vs vk::raii 三层怎么选 |
| [Present Mode](docs/notes/1.3-vulkan-present-modes.md) | 四种模式：GPU 产帧与显示器刷新怎么协调 |
| [Graphics Pipeline 不可变性](docs/notes/1.4-graphics-pipeline-immutability.md) | Pipeline 创建即封存，Vulkan 和 DX12 殊途同归 |
| [坐标空间：Clip → NDC → Screen](docs/notes/1.5-coordinate-spaces-clip-ndc.md) | P 矩阵不挤压，透视除法才挤压，z 与 w 的分工 |
| [颜色 + W 实验](docs/notes/1.6-color-w-experiment.md) | 顶点着色器输出不同 w，观察透视除法 + 颜色插值 |
| [Render Pass / Subpass / Attachment](docs/notes/1.7-renderpass-subpass-attachment.md) | 蓝图 / 步骤 / image 三层 |
| [Descriptor vs Push Constant](docs/notes/1.8-descriptor-vs-pushconstant.md) | 小参数 inline 走 push，显存资源走 descriptor 四件套 |
| [Vertex Buffer 与上传](docs/notes/1.9-vertex-buffer-and-upload.md) | 顶点搬进 GPU buffer，map/memcpy/unmap 标准姿势 |
| [Buffer 与 Memory 分配](docs/notes/1.10-buffer-memory-allocation.md) | 创建/分配/绑定三步分离，为 sub-allocation 预留 |
| [Validation Layer 架构](docs/notes/1.11-validation-layer-and-layer-architecture.md) | 链式拦截器 vs GLES hack，为什么 Vulkan 快且好调试 |
| [TBDR：RenderPass → Framebuffer](docs/notes/1.12-tbdr-renderpass-to-framebuffer.md) | tile memory 预算 → BY_REGION 流水线 → tile grid 调度 |
| [GLSL → SPIR-V 汇编](docs/notes/1.13-glsl-to-spirv-asm.md) | 高层写法编成哪些真实指令，各自有什么坑 |
| [Occupancy 与 warp 调度](docs/notes/1.14-gpu-occupancy-and-warp-scheduling.md) | 常驻/active/空闲三态、寄存器共享池、occupancy 不是越高越好 |
| [Shader 分支真身](docs/notes/1.15-branch-shapes-across-apis.md) | 真跳/拍平/step/lerp 四种形状，三栈决策差异 |

### Phase 2：内存与资源

| 笔记 | 一句话 |
|------|--------|
| [资源按更新频率分层](docs/notes/2.1-resource-update-frequency-layering.md) | per-scene/frame/material/object 四档，决定 heap 与多缓冲 |

## 📝 编码规范

- **命名**：`m_` 前缀 + PascalCase 成员，Allman 花括号，不缩写
- **`auto`**：仅右侧类型明显时使用，避免隐藏真实类型
- **注释**：英文，只解释 WHY / 这一段干嘛，不逐行复述代码
- **格式**：统一走 `.clang-format`（改完跑 `clang-format -i`）
- **构建纪律**：编译零 warning

## 🗂 文档导航

| 文档 | 用途 |
|------|------|
| [`docs/STATUS.md`](docs/STATUS.md) | 当前状态活指针（每次会话结束更新） |
| [`docs/roadmap.md`](docs/roadmap.md) | 完整学习路线与 Phase 进度 |
| [`docs/notes/`](docs/notes/) | 学习笔记（16 篇，见上表） |
| [`docs/sessions/`](docs/sessions/) | 会话日志（checkpoint，多 agent 协作） |
| [`CLAUDE.md`](CLAUDE.md) | AI 协作规范与提交纪律 |
