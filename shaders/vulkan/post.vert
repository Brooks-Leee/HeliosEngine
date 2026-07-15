#version 450

// Fullscreen triangle: no vertex buffer. The 3 vertices are generated from
// gl_VertexIndex so the triangle covers the whole screen, matching the
// swapchain extent exactly. Post subpass 1 draws this over the screen.
void main() {
    // Classic fullscreen-triangle trick: positions push far enough out that
    // two edges lie on the screen border and the third off-screen.
    vec2 pos = vec2(
        float((gl_VertexIndex << 1) & 2),  // 0, 2, 0
        float(gl_VertexIndex & 2)          // 0, 0, 2
    );
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
