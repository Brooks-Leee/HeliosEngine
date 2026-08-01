#version 450

// Pass the per-vertex color straight through — the 5-triangle scene shows each
// triangle's own red/green/blue corners (post.frag then inverts them to
// cyan/magenta/yellow). Replaces the branch-shape experiment shader (notes
// 1.13-1.15), which used gl_FragCoord to paint screen-space patterns that did
// not follow the geometry. The experiment is archived in
// experiments/branch-shape-demo.frag; its analysis lives in the notes.
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    outColor = vec4(fragColor, 1.0);
}
