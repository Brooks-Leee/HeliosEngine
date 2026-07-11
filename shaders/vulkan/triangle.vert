#version 450

// 三个顶点：不同位置、不同颜色、不同深度
vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),   // 顶部
    vec2(0.5, 0.5),    // 右下
    vec2(-0.5, 0.5)    // 左下
);

vec3 colors[3] = vec3[](
    vec3(1.0, 0.0, 0.0),   // 红 — 顶部顶点
    vec3(0.0, 1.0, 0.0),   // 绿 — 右下顶点
    vec3(0.0, 0.0, 1.0)    // 蓝 — 左下顶点
);

// 让左下顶点的 w > 1：透视除法后它会往中心收缩
// 三个顶点的 w 不一样 → 光栅化时颜色插值会做透视修正
float ws[3] = float[](
    1.0,    // 顶部：正常
    1.0,    // 右下：正常
    2.0     // 左下：w=2 → 透视除法后 x/w= -0.25, y/w= 0.25，往中心缩
);

layout(location = 0) out vec3 fragColor;

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, ws[gl_VertexIndex]);
    fragColor = colors[gl_VertexIndex];
}
