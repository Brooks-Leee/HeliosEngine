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
