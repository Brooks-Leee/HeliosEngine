#version 450

// Subpass 1 reads the color written by subpass 0 via an input attachment.
// Unlike a sampled texture, an input attachment can only be read at the
// current fragment's own pixel — there is no UV to offset, hence subpassLoad.
layout(input_attachment_index = 0, binding = 0, set = 0) uniform subpassInput inColor;

layout(location = 0) out vec4 outColor;

void main() {
    vec4 c = subpassLoad(inColor);
    // Simple post effect: invert (negative) so the wiring is obvious.
    outColor = vec4(1.0 - c.rgb, 1.0);
}
