#version 450

// Interface kept identical to the original triangle pipeline, so no C++ / pipeline changes are needed.
// All "if" variants below are driven by gl_FragCoord (per-fragment data) to force real divergence
// where intended, and to keep every case alive for the optimizer.
layout(location = 0) in vec3 fragColor;
layout(location = 0) out vec4 outColor;

void main() {
    vec2 fc = gl_FragCoord.xy;

    // CASE A: divergent if/else (condition depends on per-fragment gl_FragCoord).
    // Expectation: REAL control flow -> OpSelectionMerge + OpBranchConditional (an actual jump).
    vec3 col = fragColor;
    if (fc.x > 400.0) {
        col = vec3(1.0, 0.0, 0.0);
    } else {
        col = vec3(0.0, 0.0, 1.0);
    }

    // CASE B: ternary on a boolean -> a MASKED PICK, not a jump.
    // Expectation: OpSelect (no branch instruction at all).
    vec3 sel = (fc.y > 300.0) ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 1.0, 0.0);

    // CASE C: BOTH sides are COMPUTED, then blended with a mask (no branch, pure arithmetic).
    // Expectation: OpFMul / OpFAdd / OpFSub from mix(a, b, t) == a*(1-t) + b*t.
    vec3 a = vec3(fc.x * 0.002, 0.0, 0.0);
    vec3 b = vec3(0.0, fc.y * 0.002, 0.5);
    float t = step(400.0, fc.x);          // returns 0.0 or 1.0
    vec3 blended = mix(a, b, t);

    // CASE E: discard inside a divergent if.
    // Expectation: OpBranchConditional jumping to a block that ends in OpKill.
    if (fc.x < 50.0) {
        discard;
    }

    // CASE F: constant-folded branch.
    // Expectation: ELIMINATED. No branch opcode will appear; only a baked-in constant remains.
    vec3 cf = vec3(0.0);
    if (true) {
        cf = vec3(0.1, 0.1, 0.1);
    }

    // Combine every result so the optimizer cannot dead-code-eliminate the real cases.
    vec3 finalColor = col * 0.30 + sel * 0.25 + blended * 0.25 + cf * 0.20;
    outColor = vec4(finalColor, 1.0);
}
