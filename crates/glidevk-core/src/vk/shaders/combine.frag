#version 450
// P2 combiner fragment shader (ubershader).
//
// Implements the 3dfx Glide color + alpha combine units, driven by integer
// selectors in the push-constant block. This is the uniform-driven form; the
// integer codes match the `CombineFunction/Factor/Local/Other` enum
// discriminants in `types.rs`. Per-state shader *specialization* (emitting a
// minimal shader per combine key) is a future performance optimization — the
// math here is identical either way.
//
// Single TMU0 for now: `tex0` is the current `grTexSource` texture (or a 1x1
// white default). Multitexture (TMU1/2, grTexCombine) lands later.

layout(location = 0) in vec4 vColor;   // iterated Gouraud color
layout(location = 1) in vec2 vTex;     // sow, tow (= s/w, t/w)
layout(location = 2) in float vOow;    // 1/w
layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D tex0;

layout(push_constant) uniform Push {
    vec2 viewport;
    float ySign;
    float pad0;
    vec4 constColor;
    vec4 chromaKey;
    int cFunc; int cFactor; int cLocal; int cOther; int cInvert;
    int aFunc; int aFactor; int aLocal; int aOther; int aInvert;
    int chromaEnable;
    int alphaTestFunc;   // GR_CMP_* (7 = ALWAYS = no test)
    float alphaRef;
    float gamma;         // 3dfx output gamma (grGammaCorrectionValue); 1.0 = off
    vec2 texSize;        // bound texture dimensions (px)
    int alphaLighting;   // grAlphaControlsITRGBLighting (texAlpha MSB picks local)
    int wireframe;       // GLIDEVK_WIREFRAME: force a bright output colour
} pc;

// Recover the perspective-correct, normalized TMU0 coordinate: s = sow/oow,
// then divide by the texture size. oow==0 (unset) falls back to affine.
vec2 texCoord() {
    float w = (vOow != 0.0) ? vOow : 1.0;
    vec2 st = vTex / w;
    vec2 size = max(pc.texSize, vec2(1.0));
    return st / size;
}

// --- source selectors (GR_COMBINE_LOCAL_* / GR_COMBINE_OTHER_*) -------------
vec3 colorLocal(vec4 t) {
    // grAlphaControlsITRGBLighting: the texture alpha MSB overrides the local
    // colour source per texel — MSB set -> constant colour, clear -> iterated.
    if (pc.alphaLighting != 0) {
        return (t.a >= 0.5) ? pc.constColor.rgb : vColor.rgb;
    }
    if (pc.cLocal == 0) return vColor.rgb;      // ITERATED
    if (pc.cLocal == 1) return pc.constColor.rgb; // CONSTANT
    return vec3(0.0);                            // NONE / DEPTH
}
float alphaLocal() {
    if (pc.aLocal == 0) return vColor.a;
    if (pc.aLocal == 1) return pc.constColor.a;
    return 0.0;
}
vec3 colorOther(vec4 t) {
    if (pc.cOther == 0) return vColor.rgb;       // ITERATED
    if (pc.cOther == 1) return t.rgb;            // TEXTURE
    if (pc.cOther == 2) return pc.constColor.rgb; // CONSTANT
    return vec3(0.0);                            // NONE
}
float alphaOther(vec4 t) {
    if (pc.aOther == 0) return vColor.a;
    if (pc.aOther == 1) return t.a;
    if (pc.aOther == 2) return pc.constColor.a;
    return 0.0;
}

// --- factor (GR_COMBINE_FACTOR_*), returned as vec3 for the color path ------
vec3 factorColor(vec3 cLoc, float aLoc, float aOth, vec4 t) {
    int f = pc.cFactor;
    vec3 v;
    if (f == 0) v = vec3(0.0);                 // ZERO
    else if (f == 1) v = cLoc;                 // LOCAL
    else if (f == 2) v = vec3(aOth);           // OTHER_ALPHA
    else if (f == 3) v = vec3(aLoc);           // LOCAL_ALPHA
    else if (f == 4) v = vec3(t.a);            // TEXTURE_ALPHA
    else if (f == 7) v = vec3(1.0);            // ONE
    else if (f == 8) v = vec3(1.0) - cLoc;     // ONE_MINUS_LOCAL
    else if (f == 9) v = vec3(1.0 - aOth);     // ONE_MINUS_OTHER_ALPHA
    else if (f == 10) v = vec3(1.0 - aLoc);    // ONE_MINUS_LOCAL_ALPHA
    else if (f == 11) v = vec3(1.0 - t.a);     // ONE_MINUS_TEXTURE_ALPHA
    else v = vec3(1.0);                        // DETAIL/LOD: TODO, treat as 1
    if (pc.cInvert != 0) v = vec3(1.0) - v;
    return v;
}
float factorAlpha(float aLoc, float aOth, vec4 t) {
    int f = pc.aFactor;
    float v;
    if (f == 0) v = 0.0;
    else if (f == 1) v = aLoc;                 // LOCAL (alpha)
    else if (f == 2) v = aOth;
    else if (f == 3) v = aLoc;
    else if (f == 4) v = t.a;
    else if (f == 7) v = 1.0;
    else if (f == 8) v = 1.0 - aLoc;
    else if (f == 9) v = 1.0 - aOth;
    else if (f == 10) v = 1.0 - aLoc;
    else if (f == 11) v = 1.0 - t.a;
    else v = 1.0;
    if (pc.aInvert != 0) v = 1.0 - v;
    return v;
}

// --- combine function (GR_COMBINE_FUNCTION_*) -------------------------------
vec3 funcColor(vec3 f, vec3 loc, vec3 oth, float aLoc) {
    int fn = pc.cFunc;
    if (fn == 0) return vec3(0.0);                       // ZERO
    if (fn == 1) return loc;                             // LOCAL
    if (fn == 2) return vec3(aLoc);                      // LOCAL_ALPHA
    if (fn == 3) return f * oth;                         // SCALE_OTHER
    if (fn == 4) return f * oth + loc;                   // + LOCAL
    if (fn == 5) return f * oth + vec3(aLoc);            // + LOCAL_ALPHA
    if (fn == 6) return f * (oth - loc);                 // SCALE_OTHER_MINUS_LOCAL
    if (fn == 7) return f * (oth - loc) + loc;           // + LOCAL
    if (fn == 8) return f * (oth - loc) + vec3(aLoc);    // + LOCAL_ALPHA
    if (fn == 9) return f * (-loc) + loc;                // SCALE_MINUS_LOCAL_ADD_LOCAL
    if (fn == 10) return f * (-loc) + vec3(aLoc);        // + LOCAL_ALPHA
    return vec3(0.0);
}
float funcAlpha(float f, float loc, float oth) {
    int fn = pc.aFunc;
    if (fn == 0) return 0.0;
    if (fn == 1) return loc;
    if (fn == 2) return loc;
    if (fn == 3) return f * oth;
    if (fn == 4) return f * oth + loc;
    if (fn == 5) return f * oth + loc;
    if (fn == 6) return f * (oth - loc);
    if (fn == 7) return f * (oth - loc) + loc;
    if (fn == 8) return f * (oth - loc) + loc;
    if (fn == 9) return f * (-loc) + loc;
    if (fn == 10) return f * (-loc) + loc;
    return 0.0;
}

void main() {
    vec4 t = texture(tex0, texCoord());

    // Wireframe debug: with VK_POLYGON_MODE_LINE this fragment is a triangle
    // edge; force a bright opaque colour and skip all shading/discards so every
    // piece of geometry is visible regardless of its (possibly black) shading.
    if (pc.wireframe != 0) {
        outColor = vec4(0.0, 1.0, 0.4, 1.0);
        return;
    }

    // Chroma-key: discard fragments whose texel matches the key color.
    if (pc.chromaEnable != 0) {
        vec3 d = abs(t.rgb - pc.chromaKey.rgb);
        if (max(max(d.x, d.y), d.z) <= (1.0 / 255.0)) {
            discard;
        }
    }

    vec3 cLoc = colorLocal(t);
    vec3 cOth = colorOther(t);
    float aLoc = alphaLocal();
    float aOth = alphaOther(t);

    vec3 cf = factorColor(cLoc, aLoc, aOth, t);
    vec3 rgb = funcColor(cf, cLoc, cOth, aLoc);

    float af = factorAlpha(aLoc, aOth, t);
    float a = funcAlpha(af, aLoc, aOth);

    // Alpha test (GR_CMP_*): discard fragments that fail the comparison.
    int f = pc.alphaTestFunc;
    bool pass = true;
    if (f == 0) pass = false;                 // NEVER
    else if (f == 1) pass = a < pc.alphaRef;  // LESS
    else if (f == 2) pass = a == pc.alphaRef; // EQUAL
    else if (f == 3) pass = a <= pc.alphaRef; // LEQUAL
    else if (f == 4) pass = a > pc.alphaRef;  // GREATER
    else if (f == 5) pass = a != pc.alphaRef; // NOTEQUAL
    else if (f == 6) pass = a >= pc.alphaRef; // GEQUAL
    // f == 7 (ALWAYS) or unset: pass
    if (!pass) {
        discard;
    }

    rgb = clamp(rgb, 0.0, 1.0);
    // 3dfx RAMDAC output gamma. The hardware applies this at scan-out, lifting
    // dim flat-shaded content (Unreal's intro logos) into the visible range.
    // Applied per-fragment here; exact for single-layer geometry over black,
    // which is where it matters. gamma <= 0 (unset) is treated as identity.
    if (pc.gamma > 0.0 && pc.gamma != 1.0) {
        rgb = pow(rgb, vec3(1.0 / pc.gamma));
    }
    outColor = vec4(rgb, clamp(a, 0.0, 1.0));
}
