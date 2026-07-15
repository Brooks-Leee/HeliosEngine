# Current Status

> **活指针**—每次会话结束由 agent 更新。

## 位置

- **Phase**: 1.2 Vulkan Hello Triangle ✅ 完成；1.2.x 扩展进行中（Vertex Buffer ✅，MVP / depth 待做）
- **上次完成**: Vertex Buffer 落地——把硬编码顶点数组搬进真正的 GPU vertex buffer（createBuffer → allocateMemory(host-visible+coherent) → bindBufferMemory → map/memcpy/unmap），pipeline 加 VertexInputBinding/Attribute 描述（location 0=pos vec2, 1=color vec3），push constant 的 offset/scale 逻辑保留不变；5 个反色三角形画面与之前一致（改对验证）。顺带把 render pass/subpass/attachment、descriptor vs push constant、vertex buffer 上传、buffer-memory 三步分离四个概念系统梳理进 docs/notes（1.7–1.10）。
- **下次**: MVP 矩阵（真正的 mesh 变换，替代 offset/scale，顺带体会"大参数为何走 descriptor 而非 push constant"）；depth attachment（让 5 个三角形有前后遮挡，亲手用上 attachment 的第三种角色）。

## 入口文件

| 文件 | 用途 |
|------|------|
| `src/main.cpp` | 程序入口 |
| `src/core/Render/Vulkan/VulkanRenderer.h` | Vulkan 渲染器类声明（每个成员都有注释解释用途） |
| `src/core/Render/Vulkan/VulkanRenderer.cpp` | Vulkan 渲染器全实现（英文注释解释"为什么"） |
| `src/core/Platform/Window.h/.cpp` | Win32 RAII 窗口 |
| `docs/roadmap.md` | 全部 Phase 进度 |
| `docs/notes/` | 学习笔记（12 篇） |
| `docs/sessions/` | 每次会话的 checkpoint |

## 编码规范速查

- `m_` 前缀 + PascalCase 成员、Allman 花括号、禁用 `auto`（除 `reinterpret_cast`）、不缩写
- `.clang-format` 已配好，改代码后跑 `clang-format -i 文件.cpp`
- 注释用**英文**，只解释 WHY / 这一段干嘛，不逐行复述代码

## 最近一次会话

见 `docs/sessions/2026-07-16.md`
