#version 450

// Three vertices: a unit triangle (centered at origin), fixed red/green/blue colors
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),   // top
    vec2(0.5, 0.5),    // bottom-right
    vec2(-0.5, 0.5)    // bottom-left
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),   // red  — top vertex
    vec3(0.0, 1.0, 0.0),   // green — bottom-right vertex
    vec3(0.0, 0.0, 1.0)    // blue  — bottom-left vertex
);

// push constant: a tiny chunk of data the CPU stuffs in before each draw (<=128 bytes,
// the fastest way to pass parameters). Here it acts as "per-object transform":
// offset = translation, scale = scaling. A real engine would use a full MVP matrix here.
layout(push_constant) uniform Push {
    vec2 offset;
    float scale;
} pc;

layout(location = 0) out vec3 fragColor;

void main() {
    // Same vertices, scaled then translated -> each draw lands at a different
    // screen position and size
    vec2 p = positions[gl_VertexIndex] * pc.scale + pc.offset;
    gl_Position = vec4(p, 0.0, 1.0);
    fragColor = colors[gl_VertexIndex];
}
