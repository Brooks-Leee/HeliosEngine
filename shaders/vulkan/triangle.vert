#version 450

// Per-vertex inputs, now fed from a real vertex buffer (was hardcoded arrays before).
// location must match the VertexInputAttributeDescription set up on the C++ side.
layout(location = 0) in vec2 inPos;
layout(location = 1) in vec3 inColor;

// push constant: a tiny chunk of data the CPU stuffs in before each draw (<=128 bytes,
// the fastest way to pass parameters). Here it acts as "per-object transform":
// offset = translation, scale = scaling. A real engine would use a full MVP matrix here.
layout(push_constant) uniform Push {
    vec2 offset;
    float scale;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    // The base triangle comes from the vertex buffer; scale + offset from the
    // push constant place each draw at a different screen position and size.
    vec2 p = inPos * pc.scale + pc.offset;
    gl_Position = vec4(p, 0.0, 1.0);
    fragColor = inColor;
}
