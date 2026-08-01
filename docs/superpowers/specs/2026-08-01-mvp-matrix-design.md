# MVP 矩阵：动态偏移 UBO + DirectXMath + 3D 相机 — 设计文档

> Phase 1.2.x | 2026-08-01

## 背景与目标

base pass 现在用 push constant（`TrianglePush { offset, scale }`）把 5 个三角形放在 clip space，无相机、无矩阵、零 descriptor。目标是**真正的 MVP 矩阵**：用 uniform buffer + descriptor 替代 push constant，引入 4x4 矩阵与 3D 相机。学习目标（STATUS）："真正的 mesh 变换写法，替代 offset/scale 进 uniform buffer；体会大参数为何走 descriptor 而非 push constant"。

## 设计决策

- **矩阵数学：DirectXMath**（Windows SDK 自带、header-only、零依赖）。用户选定。
- **descriptor 结构：单个动态偏移 UBO**（方案 A）——一个 UBO 装 5 个组合 MVP，descriptor 绑一次，逐 draw 用 dynamic offset 选槽。用户选定。生产模式，直接回答"per-object 大参数为何走 descriptor + offset"。
- **相机：真 3D**——position + lookAt + perspective 投影，5 个三角形散布 z 方向。用户选定。重叠处的绘制顺序 artifact 是**预期行为**，为下一个 depth 任务埋动机。
- **UBO 内容：每槽一个组合 MVP**（`model · view · proj`，CPU 预乘，DirectXMath 行向量序，transpose 后写入）。VP 分离（频率分层）留作后续。
- **坐标互操作**：写 UBO 前 `XMMatrixTranspose`（row-major/行向量 → column-major/列向量）；投影矩阵 `_22 *= -1`（D3D y-up → Vulkan y-down）。z∈[0,1] 两边一致，无需处理。
- **UBO 内存：host-visible + coherent，持久映射，每帧 memcpy 更新**——与 vertex buffer（device-local + staging）形成频率分层对照（笔记 2.1 实操版）。
- **最小动画：相机绕 Y 缓慢旋转**（时间驱动），证明 per-frame 更新路径真实在跑。
- **base pipeline layout：删 push constant range，加 descriptor set layout**——替代完成。

## 改动点

### 1. Shader（shaders/vulkan/triangle.vert + 重编 .spv）

```glsl
#version 450
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;
layout(binding = 0) uniform PerObject { mat4 mvp; } ubo;   // 替代 push_constant 块
layout(location = 0) out vec3 fragColor;
void main() {
    gl_Position = ubo.mvp * vec4(inPos, 0.0, 1.0);
    fragColor = inColor;
}
```

用 `glslc shaders/vulkan/triangle.vert -o shaders/vulkan/triangle.vert.spv` 重编（glslc 在 `/c/VulkanSDK/1.4.350.0/Bin/`）。

### 2. UBO 创建与布局（VulkanRenderer.cpp，init 期）

```cpp
// 对齐：查 minUniformBufferOffsetAlignment（NVIDIA 常 256）
uint32_t uboStride = 64; // sizeof(mat4)
uint32_t minAlign = m_PhysicalDevice.getProperties().limits.minUniformBufferOffsetAlignment;
uboStride = ((uboStride + minAlign - 1) / minAlign) * minAlign;   // alignUp(64, minAlign)
vk::DeviceSize uboSize = 5 * uboStride;

// host-visible + coherent，持久映射一次（init 时 map，之后每帧直接 memcpy）
m_UniformBuffer = createBuffer(uboSize, eUniformBuffer, host-visible+coherent);
m_UniformBufferMapped = m_Device->mapMemory(m_UniformBufferMemory.get(), 0, uboSize);
```

- 成员：`m_UniformBuffer` / `m_UniformBufferMemory`（memory 先声明，buffer 后销毁）、`m_UniformBufferMapped`（`void*`）、`m_UniformBufferStride`（`uint32_t`）。
- **无 staging、无 eTransferDst**——per-frame 数据住 host-visible，这是频率分层教学点。

### 3. Descriptor 四件套（init 期）

```cpp
// Layout: binding 0, eUniformBufferDynamic, eVertex
// Pool:   {eUniformBufferDynamic, 1}, maxSets 1
// Set:    allocate 1
// Write:  DescriptorBufferInfo{ m_UniformBuffer.get(), 0, uboStride /* range = 对齐后一个槽 */ },
//         descriptorType = eUniformBufferDynamic
```

`range = uboStride`（≥ 64，覆盖 shader 读的一个 mat4），配合 dynamicOffset 后访问区 `[i·stride, (i+1)·stride)` 严格落在 buffer 内（i=4 时恰好到末尾）。避免 range=64 的边缘 VU 风险。

### 4. base pipeline layout：删 push constant，加 descriptor set layout

`m_PipelineLayout` 从"仅 push constant range"改为"仅 descriptor set layout"。`TrianglePush` 结构体删除。

### 5. 相机 + 每帧 UBO 更新（VulkanRenderer::Render）

```cpp
#include <DirectXMath.h>   // 注意 windows.h 的 min/max 宏：必要时 #define NOMINMAX 在 include 之前
using namespace DirectX;

float timeSec = GetTickCount64() / 1000.0f;
XMMATRIX view = XMMatrixLookAtRH(
    XMVectorSet(sinf(timeSec*0.3f)*5, 1.5f, cosf(timeSec*0.3f)*5, 0),  // eye 绕 Y
    XMVectorSet(0, 0, 0, 0), XMVectorSet(0, 1, 0, 0));
XMMATRIX proj = XMMatrixPerspectiveFovRH(XMConvertToRadians(60), aspect, 0.1f, 100.0f);
proj._22 *= -1.0f;   // Vulkan y-down

// 5 个三角形，各给 model（translation 到 3D 位置 + scale）
const XMFLOAT3 positions[5] = {...};   // z 各异 → 透视近大远小 + 重叠 artifact
for (uint32_t i = 0; i < 5; ++i)
{
    XMMATRIX model = XMMatrixTranslation(p[i].x, p[i].y, p[i].z);  // × XMMatrixScaling
    XMMATRIX mvp = model * view * proj;          // 行向量序
    XMFLOAT4X4 out;
    XMStoreFloat4x4(&out, XMMatrixTranspose(mvp));  // → GLSL column-major
    std::memcpy(static_cast<char*>(m_UniformBufferMapped) + i * uboStride, &out, sizeof(out));
}
```

### 6. RecordCommandBuffer

```cpp
bindPipeline(eGraphics, m_Pipeline.get());
setViewport / setScissor
bindVertexBuffers(0, vb)
for (uint32_t i = 0; i < 5; ++i)
{
    uint32_t dynamicOffset = i * m_UniformBufferStride;
    m_CommandBuffer.bindDescriptorSets(eGraphics, m_PipelineLayout.get(), 0, 1,
                                       &m_DescriptorSet.get(), 1, &dynamicOffset);
    m_CommandBuffer.draw(3, 1, 0, 0);
}
// 删除 pushConstants 调用
```

### 7. 其余

- `#include <DirectXMath.h>`；`NOMINMAX` 兜底。
- Shutdown 时 `unmapMemory`（或 UniqueHandle + waitIdle 已有）。
- 数据流：CPU 每帧 5×（M·V·P → transpose → memcpy）→ GPU 每 draw 按 dynamic offset 读对应 mat4。

## 验证标准

- [ ] build 零 warning
- [ ] `glslc` 重编 triangle.vert.spv 无报错
- [ ] 运行：5 个三角形以正确透视显示在 3D 空间（近大远小），**图像没有上下颠倒**（y-flip 生效），validation 无报错
- [ ] 相机绕 Y 缓慢旋转（动画生效 = per-frame UBO 更新路径在跑）
- [ ] 重叠区有绘制顺序 artifact（预期，为 depth 铺垫）
- [ ] 视觉确认（human）：布局/透视正确

## DX12 对照（写入笔记）

| Vulkan | DX12 |
|---|---|
| uniform buffer + dynamic offset | CBV + descriptor heap（一个 heap 多个 CBV，逐对象换 descriptor table / root descriptor） |
| `eUniformBufferDynamic` | D3D12_DESCRIPTOR_HEAP_TYPE_CBV + offsetting |

## 文档产物（docs 提交需用户同意后单独做）

- 更新 `docs/notes/1.8-descriptor-vs-pushconstant.md` 或新增笔记：MVP + dynamic offset 实战、row/col-major transpose、y-flip、频率分层对照（UBO host-visible vs 顶点 device-local）

## 后续（本次不做）

- **VP 分离**：把 view·proj 抽到独立 static binding（频率分层正解），届时"每帧算 5 次 VP 好蠢"即抽取时机
- **depth attachment**：修复重叠 artifact（roadmap 下一项）
- 手写 mat4 自研数学库（DirectXMath 之外的长期目标）
