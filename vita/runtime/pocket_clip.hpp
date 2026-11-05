#pragma once
// POCKET_PMO_SPLIT_V15: exact copy of Ge::draw_triangle() (PSP near-plane clipping and 4096 guard band) followed by
// VitaGpu::vertex() (screen -> GL clip space with the depth clamp), for one triangle given in PSP clip space.
// Used for the few triangles of a PMO group that need clipping, so the rest of the group can stay on the GPU
// instead of sending the whole group to the CPU path. Plain C++: tested on a PC against the real Ge.
#include <algorithm>
#include <array>
#include <cmath>

namespace pocket::clip {

struct ClipVertex { float x, y, z, w, u, v, r, g, b, a; };       // PSP clip space + attributes
struct GlVertex { float position[4], color[4], uv[2]; };          // same layout as VitaGpu's GVertex

inline ClipVertex lerp(const ClipVertex &p, const ClipVertex &q, float t) {
    auto l = [t](float x, float y) { return x + (y - x) * t; };
    return {l(p.x, q.x), l(p.y, q.y), l(p.z, q.z), l(p.w, q.w), l(p.u, q.u), l(p.v, q.v),
            l(p.r, q.r), l(p.g, q.g), l(p.b, q.b), l(p.a, q.a)};
}

// viewport = {x scale, y scale, z scale, x centre, y centre, z centre, x offset, y offset} (rigid::Transform order).
// side = GL target size (512). Colours pass through unchanged (the caller's scale), clamped to 0..limit.
// Calls emit(GlVertex) 0, 3 or 6 times; with flat shading every triangle takes the colour of its last vertex.
template <class Emit>
inline void triangle(const std::array<float, 8> &vp, const ClipVertex in[3], bool flat, float side, float limit,
                     Emit &&emit) {
    struct Screen { float x, y, z, inv_w; ClipVertex c; };
    auto to_screen = [&](const ClipVertex &c) {
        const float inv_w = c.w != 0 ? 1.0f / c.w : 1.0f;
        return Screen{c.x * inv_w * vp[0] + vp[3] - vp[6], c.y * inv_w * vp[1] + vp[4] - vp[7],
                      c.z * inv_w * vp[2] + vp[5], inv_w, c};
    };
    auto gl = [&](const Screen &s, const ClipVertex &colour) {
        GlVertex o{};
        const float w = !std::isfinite(s.inv_w) || s.inv_w <= 0 ? 1.0f : 1.0f / s.inv_w;
        o.position[0] = (s.x * (2.0f / side) - 1) * w;
        o.position[1] = (1 - s.y * (2.0f / side)) * w;
        o.position[2] = (std::clamp(s.z, 0.0f, 65535.0f) * (2.0f / 65535) - 1) * w;
        o.position[3] = w;
        o.color[0] = std::clamp(colour.r, 0.0f, limit); o.color[1] = std::clamp(colour.g, 0.0f, limit);
        o.color[2] = std::clamp(colour.b, 0.0f, limit); o.color[3] = std::clamp(colour.a, 0.0f, limit);
        o.uv[0] = s.c.u; o.uv[1] = s.c.v;
        return o;
    };
    auto out3 = [&](const Screen &a, const Screen &b, const Screen &c) {
        emit(gl(a, flat ? c.c : a.c)); emit(gl(b, flat ? c.c : b.c)); emit(gl(c, c.c));
    };
    bool any_clipped = false;
    Screen screen[3];
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &v = in[i];
        const bool inside = v.z > -v.w && v.w > 0.0f;
        if (!inside) { any_clipped = true; continue; }
        screen[i] = to_screen(v);
        const float sx = screen[i].x + vp[6], sy = screen[i].y + vp[7];
        if (!(sx >= 0.0f && sx < 4096.0f && sy >= 0.0f && sy < 4096.0f)) return; // outside the guard band
    }
    if (!any_clipped) { out3(screen[0], screen[1], screen[2]); return; }
    ClipVertex out[4];
    int n = 0;
    for (int i = 0; i < 3; ++i) {
        const ClipVertex &p = in[i], &q = in[(i + 1) % 3];
        const float dp = p.z + p.w, dq = q.z + q.w;
        const bool pin = dp > 0.0f && p.w > 0.0f, qin = dq > 0.0f && q.w > 0.0f;
        if (pin) out[n++] = p;
        if (pin != qin && dp != dq) {
            ClipVertex v = lerp(p, q, dp / (dp - dq));
            if (v.w <= 0.0f) continue;
            out[n++] = v;
        }
    }
    if (n < 3) return;
    Screen s[4];
    for (int i = 0; i < n; ++i) s[i] = to_screen(out[i]);
    out3(s[0], s[1], s[2]);
    if (n == 4) out3(s[0], s[2], s[3]);
}

} // namespace pocket::clip
