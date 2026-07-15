# 学习笔记索引

> 对话中聊透的概念，顺手记下来。每个文件聚焦一个主题，持续更新。

## Phase 1：Vulkan 基础

| 笔记 | 一句话 |
|------|--------|
| [RAII 与资源管理](1.1-raii-and-resource-management.md) | 资源生命周期绑在对象构造/析构上，不是某个头文件 |
| [Vulkan API 绑定](1.2-vulkan-api-bindings.md) | C API vs vulkan.hpp vs vk::raii 三层怎么选 |
| [Present Mode](1.3-vulkan-present-modes.md) | 四种模式：GPU 产出帧和显示器刷新怎么协调 |
| [Graphics Pipeline 不可变性](1.4-graphics-pipeline-immutability.md) | Pipeline 创建即封存，Vulkan 和 DX12 殊途同归 |
| [坐标空间：Clip → NDC → Screen](1.5-coordinate-spaces-clip-ndc.md) | P 矩阵不挤压，透视除法才挤压。z 和 w 的分工 |
| [颜色 + W 实验](1.6-color-w-experiment.md) | 顶点着色器输出不同 w，观察透视除法 + 颜色插值 |
| [Render Pass / Subpass / Attachment](1.7-renderpass-subpass-attachment.md) | 蓝图 / 步骤 / image；≥1 subpass，上屏只看 swapchain + PresentSrcKHR |
| [Descriptor vs Push Constant](1.8-descriptor-vs-pushconstant.md) | 小参数 inline 走 push，显存资源走 descriptor 四件套 |
| [Vertex Buffer 与上传](1.9-vertex-buffer-and-upload.md) | 顶点搬进 GPU buffer；map/memcpy/unmap 是标准上传姿势 |
| [Buffer 与 Memory 分配](1.10-buffer-memory-allocation.md) | 创建/分配/绑定三步分离，为 sub-allocation 预留 |
| [Validation Layer & Layer 架构](1.11-validation-layer-and-layer-architecture.md) | 链式拦截器 vs GLES hack，为什么 Vulkan 比 OpenGL 快且好调试 |
| [TBDR：RenderPass → Framebuffer](1.12-tbdr-renderpass-to-framebuffer.md) | 三阶段流水线：tile memory 预算 → BY_REGION 流水线 → tile grid 调度 |

## Phase 2+（待填充）

后续对话中逐步补充。
