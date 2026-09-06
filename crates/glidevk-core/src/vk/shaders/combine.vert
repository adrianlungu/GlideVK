#version 450
// P2/P3 combiner vertex shader: window-space position -> clip space, with a
// per-vertex depth (from GR_PARAM_Z), forwarding the iterated Gouraud color and
// TMU0 texture coordinates. The push-constant block is shared verbatim with
// combine.frag.

layout(location = 0) in vec2 inPos;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec2 inTex;   // sow, tow (= s/w, t/w)
layout(location = 3) in float inDepth;
layout(location = 4) in float inOow;  // 1/w

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vTex;
layout(location = 2) out float vOow;

layout(push_constant) uniform Push {
    vec2 viewport;
    float ySign;
    float pad0;
    vec4 constColor;
    vec4 chromaKey;
    int cFunc; int cFactor; int cLocal; int cOther; int cInvert;
    int aFunc; int aFactor; int aLocal; int aOther; int aInvert;
    int chromaEnable;
    int alphaTestFunc;
    float alphaRef;
    float pad1;
    vec2 texSize;
    vec2 pad2;
} pc;

void main() {
    float x = inPos.x / pc.viewport.x * 2.0 - 1.0;
    float y = (inPos.y / pc.viewport.y * 2.0 - 1.0) * pc.ySign;
    gl_Position = vec4(x, y, clamp(inDepth, 0.0, 1.0), 1.0);
    vColor = inColor;
    // Pass sow/tow and oow through; the fragment shader divides for
    // perspective-correct, texture-size-normalized coordinates.
    vTex = inTex;
    vOow = inOow;
}
