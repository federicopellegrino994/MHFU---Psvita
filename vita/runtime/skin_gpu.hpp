#pragma once
// POCKET_GPU_SKIN_V13: skinning, transform and PSP lighting on the Vita GPU for the models that dominate the slow
// scenes (characters and monsters: 8-bit weights, 16-bit UV, 8-bit normal, 16-bit position, 3 lights).
//
// Everything here is plain C++ without GL so it can be tested on a PC:
//   format_for()  which vertex formats the GPU path takes,
//   repack()      one guest vertex into a fixed 24-byte layout the shader reads,
//   fill_*()      the shader parameters (uniforms) from the GE registers,
//   run_vertex()  the vertex shader's arithmetic, line for line, used to compare with the CPU GE path.
// kVertexShader / kFragmentShader below must stay in step with run_vertex() and the combiner in vita_gpu.cpp.

#include "ge.hpp"
#include "rigid_gpu_math.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

namespace pocket::skin {
using mhf::psp::gpu::Ge;

// Fixed layout read by the vertex shader: weights at 0 (8 x u8, missing ones are 0), UV at 8 (2 x u16),
// normal at 12 (3 x s8), position at 16 (3 x s16). 24 bytes, all attributes 4-byte aligned.
struct Vertex {
    std::uint8_t w[8];
    std::uint16_t uv[2];
    std::int8_t n[4];
    std::int16_t p[4];
};
static_assert(sizeof(Vertex) == 24, "shader attribute offsets assume 24 bytes");

struct Format {
    std::uint32_t vt{};
    unsigned weights{};   // 0 (no skinning) or 1..8
    bool normal{};
    unsigned stride{}, w_off{}, tc_off{}, n_off{}, p_off{};
    unsigned index{};     // 0 none, 1 u8, 2 u16
};

// Weights u8 (1..8) or none, UV u16, no vertex colour, normal s8 or none, position s16, no morphing, index none/u8/u16,
// not through mode. That is the whole census of the slow scenes; anything else keeps the existing paths.
inline bool format_for(std::uint32_t vt, Format &f) {
    const unsigned tc = vt & 3u, color = (vt >> 2u) & 7u, normal = (vt >> 5u) & 3u, pos = (vt >> 7u) & 3u;
    const unsigned weight = (vt >> 9u) & 3u, index = (vt >> 11u) & 3u, morph = (vt >> 18u) & 7u;
    if (tc != 2u || color != 0u || normal > 1u || pos != 2u || weight > 1u || index == 3u || morph != 0u ||
        (vt & (1u << 23u)) != 0u)
        return false;
    const auto o = Ge::vertex_offsets(vt);
    f.vt = vt;
    f.weights = weight ? ((vt >> 14u) & 7u) + 1u : 0u;
    f.normal = normal == 1u;
    f.stride = o.size;
    f.w_off = o.weight; f.tc_off = o.tc; f.n_off = o.normal; f.p_off = o.pos;
    f.index = index;
    return true;
}

inline void repack(const std::uint8_t *src, const Format &f, Vertex &out) {
    std::memset(&out, 0, sizeof out);
    if (f.weights) std::memcpy(out.w, src + f.w_off, f.weights);
    else out.w[0] = 128; // weight 1.0 on bone 0, which fill_bones() makes the identity
    std::memcpy(out.uv, src + f.tc_off, 4);
    if (f.normal) std::memcpy(out.n, src + f.n_off, 3);
    std::memcpy(out.p, src + f.p_off, 6);
}

// Shader parameters, as float4 rows/columns exactly as uploaded with glUniform4fv.
struct Uniforms {
    float bones[24][4]{};  // bone b, row r at [b * 3 + r] = (m[r], m[3 + r], m[6 + r], m[9 + r])
    float world[3][4]{};   // world matrix rows, same convention
    float mvp[4][4]{};     // model -> GL clip (rigid Transform::gpu), as columns
    float clip[4][4]{};    // model -> PSP clip (Transform::clip), as columns: tests only, not uploaded
    float uv[4]{};         // u scale, v scale, u offset, v offset (applied to uv / 32768)
    float uvt[4]{};        // u multiplier, v multiplier, v add (render-target textures flip v), 0
    float lpos[4][4]{};    // xyz: position (point/spot) or normalized direction (directional); w: kind 0/1/2
    float ldir[4][4]{};    // xyz: spot direction; w: 1 / |direction| (0 when zero)
    float latt[4][4]{};    // xyz: attenuation constant/linear/quadratic; w: component 0/1/2
    float lspot[4][4]{};   // x: spot cutoff, y: spot exponent, z: light enabled (0/1)
    float lamb[4][4]{}, ldif[4][4]{}, lspe[4][4]{}; // light colours / 255
    float mamb[4]{}, mdif[4]{}, mspe[4]{};          // material colours / 255 (ambient alpha in mamb.w)
    float base[4]{};       // emissive + global ambient * material ambient (lit), or material ambient (unlit), / 255
    float params[4]{};     // x: lighting 0/1, y: normal sign +1/-1, z: separate specular 0/1, w: shininess
};

inline void rows(const float m[12], float out[3][4]) {
    for (unsigned r = 0; r < 3; ++r) {
        out[r][0] = m[r]; out[r][1] = m[3 + r]; out[r][2] = m[6 + r]; out[r][3] = m[9 + r];
    }
}

inline void fill_bones(const Ge &ge, unsigned weights, Uniforms &u) {
    std::memset(u.bones, 0, sizeof u.bones);
    if (weights == 0) { // no skinning: bone 0 is the identity
        u.bones[0][0] = u.bones[1][1] = u.bones[2][2] = 1.0f;
        return;
    }
    // Unused bones stay 0: their weights are 0 too, and 0 * 0 cannot turn into NaN as 0 * garbage could.
    for (unsigned b = 0; b < weights && b < 8u; ++b) rows(ge.bone_matrix(b).m.data(), &u.bones[b * 3]);
}

inline void fill_transform(const Ge &ge, const pocket::rigid::Transform &t, Uniforms &u) {
    rows(ge.world_matrix().m.data(), u.world);
    for (unsigned c = 0; c < 4; ++c)
        for (unsigned r = 0; r < 4; ++r) { u.mvp[c][r] = t.gpu[c * 4 + r]; u.clip[c][r] = t.clip[c * 4 + r]; }
}

// UV scale/offset (texture map mode 0) and the render-target adjustment vita_gpu.cpp's vertex() applies.
inline void fill_uv(const Ge &ge, bool target_texture, float texture_width, float texture_height, float side,
                    Uniforms &u) {
    using mhf::psp::gpu::float24;
    u.uv[0] = float24(ge.reg(0x48)); u.uv[1] = float24(ge.reg(0x49));
    u.uv[2] = float24(ge.reg(0x4A)); u.uv[3] = float24(ge.reg(0x4B));
    if (target_texture) { u.uvt[0] = texture_width / side; u.uvt[1] = -(texture_height / side); u.uvt[2] = 1.0f; }
    else { u.uvt[0] = 1.0f; u.uvt[1] = 1.0f; u.uvt[2] = 0.0f; }
    u.uvt[3] = 0.0f;
}

inline void unpack255(std::uint32_t abgr, float out[4]) {
    out[0] = float(abgr & 0xFFu) / 255.0f; out[1] = float((abgr >> 8u) & 0xFFu) / 255.0f;
    out[2] = float((abgr >> 16u) & 0xFFu) / 255.0f; out[3] = float((abgr >> 24u) & 0xFFu) / 255.0f;
}

// Same registers and derivations as Ge::rebuild_light(), in units of 1/255 (so the arithmetic of Ge::light() becomes
// the same arithmetic divided by 255). Vertex colours do not exist in format_for()'s family, so the material always
// supplies ambient/diffuse/specular, as in Ge::light() without a vertex colour.
inline void fill_light(const Ge &ge, Uniforms &u) {
    using mhf::psp::gpu::float24;
    float emissive[4], global[4];
    unpack255(ge.reg(0x55) | (ge.reg(0x58) << 24u), u.mamb);
    unpack255(ge.reg(0x56), u.mdif);
    unpack255(ge.reg(0x57), u.mspe);
    unpack255(ge.reg(0x54), emissive);
    unpack255(ge.reg(0x5C) | (ge.reg(0x5D) << 24u), global);
    const bool lighting = (ge.reg(0x17) & 1u) != 0;
    if (lighting) {
        for (unsigned c = 0; c < 3; ++c) u.base[c] = emissive[c] + global[c] * u.mamb[c];
        u.base[3] = global[3] * u.mamb[3];
    } else {
        std::copy(u.mamb, u.mamb + 4, u.base); // Ge::transform(): material ambient + alpha, no secondary colour
    }
    u.params[0] = lighting ? 1.0f : 0.0f;
    u.params[1] = (ge.reg(0x51) & 1u) ? -1.0f : 1.0f;
    u.params[2] = (ge.reg(0x5E) & 1u) ? 1.0f : 0.0f;
    u.params[3] = float24(ge.reg(0x5B));
    for (unsigned i = 0; i < 4; ++i) {
        const bool on = lighting && (ge.reg(0x18 + i) & 1u) != 0;
        const std::uint32_t type = ge.reg(0x5F + i);
        const float kind = float((type >> 8u) & 3u), comp = float(type & 3u);
        float pos[3], dir[3], att[3];
        for (unsigned k = 0; k < 3; ++k) {
            pos[k] = float24(ge.reg(0x63 + i * 3 + k));
            dir[k] = float24(ge.reg(0x6F + i * 3 + k));
            att[k] = float24(ge.reg(0x7B + i * 3 + k));
        }
        if (kind == 0.0f) { // directional: the normalized position is the direction to the light
            const float ll = std::sqrt(pos[0] * pos[0] + pos[1] * pos[1] + pos[2] * pos[2]);
            if (ll > 0) for (float &v : pos) v /= ll;
        }
        const float dl = std::sqrt(dir[0] * dir[0] + dir[1] * dir[1] + dir[2] * dir[2]);
        for (unsigned k = 0; k < 3; ++k) { u.lpos[i][k] = pos[k]; u.ldir[i][k] = dir[k]; u.latt[i][k] = att[k]; }
        u.lpos[i][3] = kind; u.ldir[i][3] = dl > 0 ? 1.0f / dl : 0.0f; u.latt[i][3] = comp;
        u.lspot[i][0] = float24(ge.reg(0x8B + i)); u.lspot[i][1] = float24(ge.reg(0x87 + i));
        u.lspot[i][2] = on ? 1.0f : 0.0f; u.lspot[i][3] = 0.0f;
        unpack255(ge.reg(0x8F + i * 3 + 0), u.lamb[i]);
        unpack255(ge.reg(0x8F + i * 3 + 1), u.ldif[i]);
        unpack255(ge.reg(0x8F + i * 3 + 2), u.lspe[i]);
    }
}

// pow() as the CPU path computes it, including pow(0, 0) == 1 (the shader writes the same guard).
inline float spow(float x, float y) { return x > 0.0f ? std::pow(x, y) : (y == 0.0f ? 1.0f : 0.0f); }

struct Out { float clip[4], gl[4], color[4], uv[2]; }; // PSP clip, GL clip, RGBA 0..1 (primary + secondary), UV

// The vertex shader, operation for operation.
inline Out run_vertex(const Uniforms &u, const Vertex &v) {
    Out o{};
    float R[3][4] = {};
    for (unsigned b = 0; b < 8; ++b) {
        const float w = float(v.w[b]) * (1.0f / 128.0f);
        for (unsigned r = 0; r < 3; ++r) for (unsigned k = 0; k < 4; ++k) R[r][k] += u.bones[b * 3 + r][k] * w;
    }
    const float P[4] = {float(v.p[0]) * (1.0f / 32768.0f), float(v.p[1]) * (1.0f / 32768.0f),
                        float(v.p[2]) * (1.0f / 32768.0f), 1.0f};
    const float N[3] = {float(v.n[0]) * (1.0f / 128.0f), float(v.n[1]) * (1.0f / 128.0f), float(v.n[2]) * (1.0f / 128.0f)};
    float m[4], nm[3];
    for (unsigned r = 0; r < 3; ++r) {
        m[r] = R[r][0] * P[0] + R[r][1] * P[1] + R[r][2] * P[2] + R[r][3];
        nm[r] = R[r][0] * N[0] + R[r][1] * N[1] + R[r][2] * N[2];
    }
    m[3] = 1.0f;
    for (unsigned r = 0; r < 4; ++r) {
        o.clip[r] = u.clip[0][r] * m[0] + u.clip[1][r] * m[1] + u.clip[2][r] * m[2] + u.clip[3][r];
        o.gl[r] = u.mvp[0][r] * m[0] + u.mvp[1][r] * m[1] + u.mvp[2][r] * m[2] + u.mvp[3][r];
    }
    const float uu = float(v.uv[0]) * (1.0f / 32768.0f) * u.uv[0] + u.uv[2];
    const float vv = float(v.uv[1]) * (1.0f / 32768.0f) * u.uv[1] + u.uv[3];
    o.uv[0] = uu * u.uvt[0];
    o.uv[1] = vv * u.uvt[1] + u.uvt[2];

    float c[4] = {u.base[0], u.base[1], u.base[2], u.base[3]};
    float s[3] = {0, 0, 0};
    if (u.params[0] > 0.5f) {
        float wp[3], wn[3];
        for (unsigned r = 0; r < 3; ++r) {
            wp[r] = u.world[r][0] * m[0] + u.world[r][1] * m[1] + u.world[r][2] * m[2] + u.world[r][3];
            wn[r] = u.world[r][0] * nm[0] + u.world[r][1] * nm[1] + u.world[r][2] * nm[2];
        }
        const float nl = std::sqrt(wn[0] * wn[0] + wn[1] * wn[1] + wn[2] * wn[2]);
        const float ni = nl > 0.0f ? u.params[1] / nl : u.params[1];
        const float n[3] = {wn[0] * ni, wn[1] * ni, wn[2] * ni};
        for (unsigned i = 0; i < 4; ++i) {
            if (u.lspot[i][2] < 0.5f) continue;
            float L[3] = {u.lpos[i][0], u.lpos[i][1], u.lpos[i][2]};
            float att = 1.0f;
            if (u.lpos[i][3] > 0.5f) { // point or spot
                L[0] -= wp[0]; L[1] -= wp[1]; L[2] -= wp[2];
                const float d = std::sqrt(L[0] * L[0] + L[1] * L[1] + L[2] * L[2]);
                const float den = u.latt[i][0] + u.latt[i][1] * d + u.latt[i][2] * d * d;
                att = den > 0.0f ? std::min(1.0f, 1.0f / den) : 1.0f;
                if (d > 0.0f) { const float id = 1.0f / d; L[0] *= id; L[1] *= id; L[2] *= id; }
                if (u.lpos[i][3] > 1.5f) { // spot
                    float sp = u.ldir[i][3] > 0.0f
                                   ? -(L[0] * u.ldir[i][0] + L[1] * u.ldir[i][1] + L[2] * u.ldir[i][2]) * u.ldir[i][3]
                                   : 0.0f;
                    sp = sp >= u.lspot[i][0] ? spow(std::max(sp, 0.0f), u.lspot[i][1]) : 0.0f;
                    att *= sp;
                }
            }
            float dot = std::max(0.0f, n[0] * L[0] + n[1] * L[1] + n[2] * L[2]);
            if (u.latt[i][3] > 1.5f) dot = spow(dot, u.params[3]);
            for (unsigned k = 0; k < 3; ++k) c[k] += att * (u.lamb[i][k] * u.mamb[k] + u.ldif[i][k] * u.mdif[k] * dot);
            if (u.latt[i][3] > 0.5f && u.latt[i][3] < 1.5f && dot > 0.0f) {
                const float h[3] = {L[0], L[1], L[2] + 1.0f};
                const float hl = std::sqrt(h[0] * h[0] + h[1] * h[1] + h[2] * h[2]);
                const float sv = hl > 0.0f ? spow(std::max(0.0f, (n[0] * h[0] + n[1] * h[1] + n[2] * h[2]) / hl), u.params[3]) : 0.0f;
                for (unsigned k = 0; k < 3; ++k) s[k] += att * u.lspe[i][k] * u.mspe[k] * sv;
            }
        }
        if (u.params[2] < 0.5f) { for (unsigned k = 0; k < 3; ++k) { c[k] += s[k]; s[k] = 0.0f; } }
    }
    // Ge::light() clamps primary and secondary to 255 each; vita_gpu.cpp's vertex() adds them and clamps to 0..255.
    for (unsigned k = 0; k < 3; ++k) o.color[k] = std::clamp(std::min(c[k], 1.0f) + std::min(s[k], 1.0f), 0.0f, 1.0f);
    o.color[3] = std::clamp(std::min(c[3], 1.0f), 0.0f, 1.0f);
    return o;
}

// The vertex shader (Cg, compiled at run time by vitaGL with libshacccg). Mirrors run_vertex().
inline constexpr const char *kVertexShader = R"(
float spow(float x, float y) { return x > 0.0 ? pow(x, y) : (y == 0.0 ? 1.0 : 0.0); }
void main(
    float4 aW0, float4 aW1, float2 aUV, float3 aN, float3 aP,
    uniform float4 uBones[24], uniform float4 uWorld[3], uniform float4 uMvp[4],
    uniform float4 uUv, uniform float4 uUvt,
    uniform float4 uLpos[4], uniform float4 uLdir[4], uniform float4 uLatt[4], uniform float4 uLspot[4],
    uniform float4 uLamb[4], uniform float4 uLdif[4], uniform float4 uLspe[4],
    uniform float4 uMamb, uniform float4 uMdif, uniform float4 uMspe, uniform float4 uBase, uniform float4 uParams,
    float4 out vPosition : POSITION, float4 out vColor : COLOR, float2 out vTexcoord : TEXCOORD0)
{
    float4 w0 = aW0 * (1.0 / 128.0);
    float4 w1 = aW1 * (1.0 / 128.0);
    float4 R0 = uBones[0] * w0.x + uBones[3] * w0.y + uBones[6] * w0.z + uBones[9] * w0.w
              + uBones[12] * w1.x + uBones[15] * w1.y + uBones[18] * w1.z + uBones[21] * w1.w;
    float4 R1 = uBones[1] * w0.x + uBones[4] * w0.y + uBones[7] * w0.z + uBones[10] * w0.w
              + uBones[13] * w1.x + uBones[16] * w1.y + uBones[19] * w1.z + uBones[22] * w1.w;
    float4 R2 = uBones[2] * w0.x + uBones[5] * w0.y + uBones[8] * w0.z + uBones[11] * w0.w
              + uBones[14] * w1.x + uBones[17] * w1.y + uBones[20] * w1.z + uBones[23] * w1.w;
    float4 P = float4(aP * (1.0 / 32768.0), 1.0);
    float3 N = aN * (1.0 / 128.0);
    float4 m = float4(dot(R0, P), dot(R1, P), dot(R2, P), 1.0);
    float3 nm = float3(dot(R0.xyz, N), dot(R1.xyz, N), dot(R2.xyz, N));
    vPosition = uMvp[0] * m.x + uMvp[1] * m.y + uMvp[2] * m.z + uMvp[3];
    float2 t = aUV * (1.0 / 32768.0) * uUv.xy + uUv.zw;
    vTexcoord = float2(t.x * uUvt.x, t.y * uUvt.y + uUvt.z);

    float4 c = uBase;
    float3 s = float3(0.0, 0.0, 0.0);
    if (uParams.x > 0.5) {
        float3 wp = float3(dot(uWorld[0], m), dot(uWorld[1], m), dot(uWorld[2], m));
        float3 wn = float3(dot(uWorld[0].xyz, nm), dot(uWorld[1].xyz, nm), dot(uWorld[2].xyz, nm));
        float nl = length(wn);
        float3 n = wn * (nl > 0.0 ? uParams.y / nl : uParams.y);
        for (int i = 0; i < 4; i++) {
            if (uLspot[i].z > 0.5) {
                float3 L = uLpos[i].xyz;
                float att = 1.0;
                if (uLpos[i].w > 0.5) {
                    L = L - wp;
                    float d = length(L);
                    float den = uLatt[i].x + uLatt[i].y * d + uLatt[i].z * d * d;
                    att = den > 0.0 ? min(1.0, 1.0 / den) : 1.0;
                    if (d > 0.0) L = L * (1.0 / d);
                    if (uLpos[i].w > 1.5) {
                        float sp = uLdir[i].w > 0.0 ? -dot(L, uLdir[i].xyz) * uLdir[i].w : 0.0;
                        sp = sp >= uLspot[i].x ? spow(max(sp, 0.0), uLspot[i].y) : 0.0;
                        att = att * sp;
                    }
                }
                float d0 = max(0.0, dot(n, L));
                if (uLatt[i].w > 1.5) d0 = spow(d0, uParams.w);
                c.xyz = c.xyz + att * (uLamb[i].xyz * uMamb.xyz + uLdif[i].xyz * uMdif.xyz * d0);
                if (uLatt[i].w > 0.5 && uLatt[i].w < 1.5 && d0 > 0.0) {
                    float3 h = float3(L.x, L.y, L.z + 1.0);
                    float hl = length(h);
                    float sv = hl > 0.0 ? spow(max(0.0, dot(n, h) / hl), uParams.w) : 0.0;
                    s = s + att * uLspe[i].xyz * uMspe.xyz * sv;
                }
            }
        }
        if (uParams.z < 0.5) { c.xyz = c.xyz + s; s = float3(0.0, 0.0, 0.0); }
    }
    float3 one = float3(1.0, 1.0, 1.0);
    vColor = float4(saturate(min(c.xyz, one) + min(s, one)), saturate(min(c.w, 1.0)));
}
)";

// The fragment shader: the PSP texture function as vita_gpu.cpp's texture_combiner() sets it up with GL_COMBINE,
// colour doubling, and the alpha test (custom shaders bypass vitaGL's fixed-function alpha test).
// uTex: x texturing 0/1, y function 0..4, z RGBA 0/1, w colour scale 1/2. uAlpha: x test 0/1, y function 0..7, z ref.
inline constexpr const char *kFragmentShader = R"(
float4 main(float4 vColor : COLOR, float2 vTexcoord : TEXCOORD0,
            uniform sampler2D uTexture, uniform float4 uTex, uniform float4 uEnv, uniform float4 uAlpha) : COLOR
{
    float4 p = vColor;
    float4 o = p;
    if (uTex.x > 0.5) {
        float4 t = tex2D(uTexture, vTexcoord);
        float f = uTex.y;
        bool rgba = uTex.z > 0.5;
        if (f < 0.5)      { o.rgb = t.rgb * p.rgb;                               o.a = rgba ? t.a * p.a : p.a; }
        else if (f < 1.5) { o.rgb = rgba ? lerp(p.rgb, t.rgb, t.a) : t.rgb;      o.a = p.a; }
        else if (f < 2.5) { o.rgb = uEnv.rgb * t.rgb + p.rgb * (1.0 - t.rgb);    o.a = rgba ? t.a * p.a : p.a; }
        else if (f < 3.5) { o.rgb = t.rgb;                                       o.a = rgba ? t.a : p.a; }
        else              { o.rgb = t.rgb + p.rgb;                               o.a = rgba ? t.a * p.a : p.a; }
        o.rgb = saturate(o.rgb * uTex.w);
        o.a = saturate(o.a);
    }
    if (uAlpha.x > 0.5) {
        // POCKET_GPU_SKIN_V13C: no reserved words ("pass" is a Cg keyword) and no nested boolean ternaries.
        float a = o.a;
        float r = uAlpha.z;
        float g = uAlpha.y;
        float keep = 1.0;
        if (g < 0.5) keep = 0.0;
        else if (g < 1.5) keep = 1.0;
        else if (g < 2.5) keep = (a == r) ? 1.0 : 0.0;
        else if (g < 3.5) keep = (a != r) ? 1.0 : 0.0;
        else if (g < 4.5) keep = (a < r) ? 1.0 : 0.0;
        else if (g < 5.5) keep = (a <= r) ? 1.0 : 0.0;
        else if (g < 6.5) keep = (a > r) ? 1.0 : 0.0;
        else keep = (a >= r) ? 1.0 : 0.0;
        if (keep < 0.5) discard;
    }
    return o;
}
)";

// Used only if kFragmentShader does not compile on the device (the vitaGL build may not keep compiler logs):
// texture * colour (or colour alone) and the alpha test "greater or equal" / "greater" forms, enough to measure the
// path and draw most materials correctly. The log says when this one is in use.
inline constexpr const char *kFragmentShaderSimple = R"(
float4 main(float4 vColor : COLOR, float2 vTexcoord : TEXCOORD0,
            uniform sampler2D uTexture, uniform float4 uTex, uniform float4 uAlpha) : COLOR
{
    float4 o = vColor;
    if (uTex.x > 0.5) o = tex2D(uTexture, vTexcoord) * vColor;
    if (uAlpha.x > 0.5) {
        if (o.a < uAlpha.z) discard;
    }
    return o;
}
)";

} // namespace pocket::skin
