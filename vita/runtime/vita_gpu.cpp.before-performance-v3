// Experimental Vita hardware GE backend. Rasterization uses vitaGL/GXM, not the CPU.
// This is a compatibility bring-up, NOT a complete PSP GE implementation.
#include "vita_gpu.hpp"
#include "mhf/psp/host.hpp"
#include "ge.hpp"
#include "pocket_pixel.hpp"
#include <vitaGL.h>
#include <psp2/kernel/processmgr.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using namespace mhf::psp;
using gpu::Ge;
using gpu::Vertex;
struct Cost {
    std::uint64_t &total;
    std::chrono::steady_clock::time_point start = std::chrono::steady_clock::now();
    explicit Cost(std::uint64_t &v) : total(v) {}
    ~Cost() { total += std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count(); }
};
constexpr unsigned Side = 512;
constexpr std::size_t TextureBudget = 16u * 1024u * 1024u;
unsigned offset(unsigned address) { return address & 0x1fffffu; }
GLenum compare(unsigned f) {
    static constexpr GLenum v[] = {GL_NEVER, GL_ALWAYS, GL_EQUAL, GL_NOTEQUAL,
                                   GL_LESS, GL_LEQUAL, GL_GREATER, GL_GEQUAL};
    return v[f & 7];
}
struct GVertex { float position[4], color[4], uv[2]; };
struct Target { GLuint fbo{}, color{}, depth{}; unsigned stride{}, format{}; };
struct Texture { GLuint id{}; unsigned width{}, height{}; std::uint64_t stamp{}; };
class VitaGpu final : public gpu::Backend {
    Runtime &runtime;
    std::map<unsigned, Target> targets;
    std::map<std::uint64_t, Texture> textures;
    std::vector<GVertex> batch;
    std::vector<std::uint32_t> pixels;
    std::size_t texture_bytes = 0;
    std::uint64_t serial = 0;
    unsigned frames = 0, texture_width = 1, texture_height = 1;
    struct Profile {
        std::uint64_t hash{}, decode{}, upload{}, setup{}, submit{}, flush{}, swap{};
        unsigned requests{}, hits{}, misses{}, draws{}, triangles{};
    } profile;
    unsigned target_offset = 0;
    bool prepared = false, through = true, target_texture = false, flat = false;
    bool clearing = false;
    unsigned warned = 0;
    std::chrono::steady_clock::time_point last_perf = std::chrono::steady_clock::now();
    unsigned last_frames = 0;
    void warn(unsigned bit, const char *text) {
        if (!(warned & bit)) { warned |= bit; std::fprintf(stderr, "[gpu] limitation: %s\n", text); }
    }
    void erase_target(std::map<unsigned, Target>::iterator i) {
        glDeleteFramebuffers(1, &i->second.fbo);
        glDeleteRenderbuffers(1, &i->second.depth);
        glDeleteTextures(1, &i->second.color);
        targets.erase(i);
    }
    void plain_state() {
        glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND); glDisable(GL_DEPTH_TEST);
        glDisable(GL_ALPHA_TEST); glDisable(GL_CULL_FACE); glDisable(GL_LIGHTING);
        glDisable(GL_FOG); glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        glDepthMask(GL_FALSE);
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glMatrixMode(GL_TEXTURE); glLoadIdentity();
        glMatrixMode(GL_MODELVIEW);
    }
    Target &target(unsigned address, unsigned stride, unsigned format) {
        const unsigned key = offset(address);
        auto found = targets.find(key);
        if (found != targets.end() && (found->second.stride != stride || found->second.format != format)) {
            erase_target(found); found = targets.end();
        }
        if (found != targets.end()) return found->second;
        if (targets.size() >= 6) {
            // Do not evict a live render target silently: its GPU contents may be used as a texture.
            throw std::runtime_error("GPU target budget exceeded (6); additional target handling required");
        }
        Target t; t.stride = stride; t.format = format;
        glGenTextures(1, &t.color); glBindTexture(GL_TEXTURE_2D, t.color);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        pixels.assign(Side * Side, 0);
        const unsigned bpp = format == 3 ? 4 : 2;
        for (unsigned y = 0; y < Side; ++y) {
            const auto *row = runtime.memory().raw_pointer(0x04000000u + key + y * stride * bpp, stride * bpp);
            if (!row) break;
            for (unsigned x = 0; x < std::min(stride, Side); ++x)
                pixels[(Side - 1 - y) * Side + x] = pocket::unpack_pixel(row + x * bpp, format);
        }
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, Side, Side, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        glGenFramebuffers(1, &t.fbo); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.color, 0);
        glGenRenderbuffers(1, &t.depth); glBindRenderbuffer(GL_RENDERBUFFER, t.depth);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT16, Side, Side);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, t.depth);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            throw std::runtime_error("vitaGL framebuffer incomplete");
        glDisable(GL_SCISSOR_TEST); glDepthMask(GL_TRUE); glClearDepthf(1.0f); glClear(GL_DEPTH_BUFFER_BIT);
        std::fprintf(stderr, "[gpu] target %06X stride %u format %u\n", key, stride, format);
        return targets.emplace(key, t).first->second;
    }
    GLuint texture(const Ge &ge) {
        const auto info = ge.texture_info();
        texture_width = info.width; texture_height = info.height; target_texture = false;
        if (info.width > Side || info.height > Side || info.format > 10)
            throw std::runtime_error("unsupported GPU texture dimensions/format");
        const auto rt = targets.find(offset(info.address));
        if ((info.address & 0x1f000000u) == 0x04000000u && rt != targets.end() && info.format <= 3) {
            if (rt->first != target_offset) {
                target_texture = true;
                return rt->second.color;
            }
            warn(1, "texture feedback from current target samples guest memory");
        }
        ++profile.requests;
        std::uint64_t hash;
        { Cost cost(profile.hash); hash = ge.texture_hash(); }
        auto i = textures.find(hash);
        if (i != textures.end()) { ++profile.hits; i->second.stamp = serial; return i->second.id; }
        ++profile.misses;
        const std::size_t bytes = std::size_t(info.width) * info.height * 4;
        while (texture_bytes + bytes > TextureBudget && !textures.empty()) {
            auto oldest = std::min_element(textures.begin(), textures.end(), [](const auto &a, const auto &b) {
                return a.second.stamp < b.second.stamp;
            });
            glDeleteTextures(1, &oldest->second.id);
            texture_bytes -= std::size_t(oldest->second.width) * oldest->second.height * 4;
            textures.erase(oldest);
        }
        Texture t; t.width = info.width; t.height = info.height; t.stamp = serial;
        pixels.resize(std::size_t(info.width) * info.height);
        { Cost cost(profile.decode); ge.decode_texture(pixels.data()); }
        {
            Cost cost(profile.upload);
            glGenTextures(1, &t.id); glBindTexture(GL_TEXTURE_2D, t.id);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, info.width, info.height, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        }
        textures.emplace(hash, t); texture_bytes += bytes;
        return t.id;
    }
    GLenum blend_factor(unsigned f, bool source, unsigned fixed) {
        switch (f) {
        case 0: return source ? GL_DST_COLOR : GL_SRC_COLOR;
        case 1: return source ? GL_ONE_MINUS_DST_COLOR : GL_ONE_MINUS_SRC_COLOR;
        case 2: return GL_SRC_ALPHA; case 3: return GL_ONE_MINUS_SRC_ALPHA;
        case 4: return GL_DST_ALPHA; case 5: return GL_ONE_MINUS_DST_ALPHA;
        default:
            if (f < 10) warn(2, "doubled alpha blend factors approximated");
            if (f == 6) return GL_SRC_ALPHA;
            if (f == 7) return GL_ONE_MINUS_SRC_ALPHA;
            if (f == 8) return GL_DST_ALPHA;
            if (f == 9) return GL_ONE_MINUS_DST_ALPHA;
            if ((fixed & 0xffffff) != 0 && (fixed & 0xffffff) != 0xffffff)
                warn(4, "nontrivial fixed blend colors approximated");
            return (fixed & 0xffffff) == 0 ? GL_ZERO : GL_ONE;
        }
    }
    // Match the RGB/RGBA distinction in the reference software rasterizer.
    void texture_combiner(unsigned func) {
        const unsigned mode = std::min(4u, func & 7);
        const bool rgba = (func & 0x100) != 0;
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
        glTexEnvi(GL_TEXTURE_ENV, GL_RGB_SCALE, (func & 0x10000) ? 2 : 1);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_RGB, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC2_RGB, GL_TEXTURE);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_RGB, GL_SRC_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_ALPHA);
        static constexpr GLint ops[] = {GL_MODULATE, GL_REPLACE, GL_INTERPOLATE, GL_REPLACE, GL_ADD};
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB, mode == 1 && rgba ? GL_INTERPOLATE : ops[mode]);
        if (mode == 2) { // env * texture + primary * (1-texture)
            glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_RGB, GL_CONSTANT);
            glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND2_RGB, GL_SRC_COLOR);
        }
        const bool alpha_modulate = rgba && mode != 1 && mode != 3;
        glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA, alpha_modulate ? GL_MODULATE : GL_REPLACE);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC0_ALPHA, rgba && mode != 1 ? GL_TEXTURE : GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_SRC1_ALPHA, GL_PRIMARY_COLOR);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND1_ALPHA, GL_SRC_ALPHA);
        glTexEnvi(GL_TEXTURE_ENV, GL_ALPHA_SCALE, 1);
    }
    void prepare(const Ge &ge) {
        if (prepared) return;
        Cost setup_cost(profile.setup);
        target_offset = ge.reg(0x9c) & 0x1ffff0;
        auto &rt = target(target_offset, ge.reg(0x9d) & 0x7fc, ge.reg(0xd2) & 3);
        glBindFramebuffer(GL_FRAMEBUFFER, rt.fbo); glViewport(0, 0, Side, Side);
        plain_state();
        const int x1 = ge.reg(0xd4) & 1023, y1 = (ge.reg(0xd4) >> 10) & 1023;
        const int x2 = std::min(511u, ge.reg(0xd5) & 1023), y2 = std::min(511u, (ge.reg(0xd5) >> 10) & 1023);
        glEnable(GL_SCISSOR_TEST);
        glScissor(x1, int(Side) - y2 - 1, std::max(0, x2 - x1 + 1), std::max(0, y2 - y1 + 1));
        through = (ge.reg(0x12) & (1u << 23)) != 0;
        flat = (ge.reg(0x50) & 1) == 0;
        const unsigned clear = ge.reg(0xd3);
        clearing = (clear & 1) != 0;

        if (clear & 1) {
            glColorMask((clear & 0x100) != 0, (clear & 0x100) != 0, (clear & 0x100) != 0, (clear & 0x200) != 0);
            glEnable(GL_DEPTH_TEST); glDepthFunc(GL_ALWAYS); glDepthMask((clear & 0x400) != 0);
            glDisable(GL_TEXTURE_2D);
        } else {
            const unsigned mask = ge.reg(0xe8);
            glColorMask((mask & 255) != 255, ((mask >> 8) & 255) != 255,
                        ((mask >> 16) & 255) != 255, (ge.reg(0xe9) & 255) != 255);
            if (ge.reg(0x23) & 1) {
                glEnable(GL_DEPTH_TEST); glDepthFunc(compare(ge.reg(0xde)));
                glDepthMask((ge.reg(0xe7) & 1) == 0);
            }
            if (ge.reg(0x22) & 1) {
                const unsigned at = ge.reg(0xdb);
                if (((at >> 16) & 255) != 255) warn(8, "masked alpha test uses unmasked reference");
                glEnable(GL_ALPHA_TEST); glAlphaFunc(compare(at), float((at >> 8) & 255) / 255);
            }
            if (ge.reg(0x21) & 1) {
                const unsigned mode = ge.reg(0xdf);
                static constexpr GLenum eq[] = {GL_FUNC_ADD, GL_FUNC_SUBTRACT, GL_FUNC_REVERSE_SUBTRACT, GL_MIN, GL_MAX, GL_MAX};
                glEnable(GL_BLEND); glBlendEquationSeparate(eq[std::min(5u, (mode >> 8) & 15)], GL_FUNC_ADD);
                glBlendFuncSeparate(blend_factor(mode & 15, true, ge.reg(0xe0)),
                                    blend_factor((mode >> 4) & 15, false, ge.reg(0xe1)), GL_ONE, GL_ZERO);
            }
            if (ge.reg(0x1e) & 1) {
                const GLuint id = texture(ge); glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, id);
                const bool linear = (ge.reg(0xc6) & 0x100) != 0;
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, linear ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, (ge.reg(0xc7) & 1) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, (ge.reg(0xc7) & 0x100) ? GL_CLAMP_TO_EDGE : GL_REPEAT);
                texture_combiner(ge.reg(0xc9));
                GLfloat color[] = {float(ge.reg(0xca) & 255) / 255, float((ge.reg(0xca) >> 8) & 255) / 255,
                                   float((ge.reg(0xca) >> 16) & 255) / 255, 1};
                glTexEnvfv(GL_TEXTURE_ENV, GL_TEXTURE_ENV_COLOR, color);

            } else glDisable(GL_TEXTURE_2D);
            if (ge.reg(0x24) & 1) warn(32, "PSP stencil operations not implemented");
            if (!through && (ge.reg(0x1f) & 1)) warn(64, "per-vertex PSP fog not implemented");
        }
        prepared = true;
    }
    GVertex vertex(const Ge &, const Vertex &p, const Vertex &color) {
        GVertex out{};
        // Vertex positions already include GE transform, viewport and clipping.
        const float w = through || !std::isfinite(p.w) || p.w <= 0 ? 1.0f : 1.0f / p.w;
        out.position[0] = (p.x * (2.0f / Side) - 1) * w;
        out.position[1] = (1 - p.y * (2.0f / Side)) * w;
        out.position[2] = (std::clamp(p.z, 0.0f, 65535.0f) * (2.0f / 65535) - 1) * w;
        out.position[3] = w;
        out.color[0] = std::clamp(color.r + (clearing ? 0 : color.sr), 0.0f, 255.0f) / 255;
        out.color[1] = std::clamp(color.g + (clearing ? 0 : color.sg), 0.0f, 255.0f) / 255;
        out.color[2] = std::clamp(color.b + (clearing ? 0 : color.sb), 0.0f, 255.0f) / 255;
        out.color[3] = std::clamp(color.a, 0.0f, 255.0f) / 255;
        out.uv[0] = through ? p.u / texture_width : p.u;
        out.uv[1] = through ? p.v / texture_height : p.v;
        // GE transform() already applies PSP texture scale/offset in 3D.
        if (target_texture) {
            out.uv[0] *= float(texture_width) / Side;
            out.uv[1] = 1 - out.uv[1] * (float(texture_height) / Side);
        }
        return out;
    }
    void draw_batch() {
        if (batch.empty()) return;
        Cost submit_cost(profile.submit);
        ++profile.draws;
        profile.triangles += static_cast<unsigned>(batch.size() / 3);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(4, GL_FLOAT, sizeof(GVertex), batch[0].position);
        glColorPointer(4, GL_FLOAT, sizeof(GVertex), batch[0].color);
        glTexCoordPointer(2, GL_FLOAT, sizeof(GVertex), batch[0].uv);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.size()));
        batch.clear();
    }
    void screen_quad(GLuint image, float u, float top, float bottom) {
        glEnable(GL_TEXTURE_2D); glBindTexture(GL_TEXTURE_2D, image);
        glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
        glColor4f(1, 1, 1, 1);
        glBegin(GL_TRIANGLE_STRIP);
        glTexCoord2f(0, top); glVertex2f(-1, 1);
        glTexCoord2f(0, bottom); glVertex2f(-1, -1);
        glTexCoord2f(u, top); glVertex2f(1, 1);
        glTexCoord2f(u, bottom); glVertex2f(1, -1);
        glEnd();
    }
public:
    explicit VitaGpu(Runtime &rt) : runtime(rt) { batch.reserve(12288); }
    bool init() {
        std::fprintf(stderr, "[gpu] initializing vitaGL on GE worker; native rasterization test v2-profile\n");
        // vitaGL returns whether the requested display resolution was reduced,
        // NOT an initialization success flag (vgl.c, return res_fallback).
        const GLboolean reduced = vglInitExtended(1024 * 1024, 960, 544, 8 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
        std::fprintf(stderr, "[gpu] vitaGL initialized; resolution fallback=%u\n", unsigned(reduced));
        vglWaitVblankStart(GL_FALSE);
        plain_state();
        std::fprintf(stderr, "[gpu] hardware backend active; guest 480x272, display 960x544\n");
        return true;
    }
    void draw_started(const gpu::DrawInfo &) override { draw_batch(); prepared = false; ++serial; }
    void triangle(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) override {
        prepare(ge);
        batch.push_back(vertex(ge, a, flat ? c : a));
        batch.push_back(vertex(ge, b, flat ? c : b)); batch.push_back(vertex(ge, c, c));
        if (batch.size() >= 12288) draw_batch();
    }
    void sprite(const Ge &ge, const Vertex &a, const Vertex &b) override {
        Vertex tl = b, tr = b, bl = b;
        tl.x = a.x; tl.y = a.y; tl.u = a.u; tl.v = a.v;
        tr.y = a.y; tr.v = a.v; bl.x = a.x; bl.u = a.u;
        tl.w = tr.w = bl.w = 1;
        Vertex br = b; br.w = 1;
        triangle(ge, tl, tr, bl); triangle(ge, tr, br, bl);
    }
    void flush() override { draw_batch(); { Cost cost(profile.flush); glFlush(); } prepared = false; }
    void block_transfer(unsigned, unsigned, unsigned, unsigned, unsigned dst, unsigned, unsigned, unsigned,
                        unsigned, unsigned, unsigned) override {
        draw_batch(); prepared = false;
        // Source GPU data is not mirrored in guest VRAM in this first bring-up.
        // Explicit diagnostic rather than claiming readback/copy compatibility.
        warn(128, "block transfers use guest memory; GPU readback not implemented");
        const auto i = targets.find(offset(dst));
        if (i != targets.end()) erase_target(i);
    }
    void present(unsigned address, unsigned stride, unsigned format) override {
        draw_batch(); prepared = false;
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glViewport(0, 0, 960, 544); plain_state();
        auto i = targets.find(offset(address));
        if (i != targets.end()) {
            screen_quad(i->second.color, 480.0f / Side, 1, 1 - 272.0f / Side);
        } else {
            if (!stride || stride > 2048 || format > 3) return;
            const unsigned bpp = format == 3 ? 4 : 2;
            const auto *src = runtime.memory().raw_pointer(address, (std::size_t(stride) * 271 + 480) * bpp);
            if (!src) return;
            pixels.resize(480 * 272);
            for (unsigned y = 0; y < 272; ++y) for (unsigned x = 0; x < 480; ++x)
                pixels[y * 480 + x] = pocket::unpack_pixel(src + (y * stride + x) * bpp, format);
            GLuint cpu; glGenTextures(1, &cpu); glBindTexture(GL_TEXTURE_2D, cpu);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 480, 272, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
            screen_quad(cpu, 1, 0, 1); glDeleteTextures(1, &cpu);
        }
        { Cost cost(profile.swap); vglSwapBuffers(GL_FALSE); }
        ++frames;
        const auto now = std::chrono::steady_clock::now();
        const float seconds = std::chrono::duration<float>(now - last_perf).count();
        if (frames == 1 || seconds >= 5) {
            std::fprintf(stderr, "[gpu] presents %u; %.2f fps; %u targets; %u cached textures\n",
                         frames, (frames - last_frames) / std::max(0.001f, seconds),
                         static_cast<unsigned>(targets.size()), static_cast<unsigned>(textures.size()));
            const double per_frame = 1.0 / (1e6 * std::max(1u, frames - last_frames));
            std::fprintf(stderr, "[gpu-cost] ms/frame: setup_total=%.2f hash=%.2f decode=%.2f upload=%.2f submit=%.2f flush=%.2f swap=%.2f\n",
                profile.setup * per_frame, profile.hash * per_frame, profile.decode * per_frame,
                profile.upload * per_frame, profile.submit * per_frame, profile.flush * per_frame, profile.swap * per_frame);
            std::fprintf(stderr, "[gpu-count] period: requests=%u hits=%u misses=%u draws=%u triangles=%u\n",
                profile.requests, profile.hits, profile.misses, profile.draws, profile.triangles);
            profile = {};
            last_perf = now; last_frames = frames;
            const GLenum error = glGetError();
            if (error) std::fprintf(stderr, "[gpu] GL error %04X\n", error);
        }
    }
};
// A failed GPU command must leave a useful log, not escape the GE worker as an
// uncaught C++ exception. Restore software.flag for the next boot if this fires.
class GuardedGpu final : public gpu::Backend {
    std::unique_ptr<VitaGpu> gpu;
    template<class F> void invoke(F &&f) {
        try { f(); }
        catch (const std::exception &e) {
            std::fprintf(stderr, "[gpu] fatal renderer error: %s\n", e.what());
            std::fflush(stderr); sceKernelExitProcess(3);
        }
    }
public:
    explicit GuardedGpu(std::unique_ptr<VitaGpu> p) : gpu(std::move(p)) {}
    void draw_started(const gpu::DrawInfo &d) override { invoke([&]{ gpu->draw_started(d); }); }
    void triangle(const Ge &g, const Vertex &a, const Vertex &b, const Vertex &c) override {
        invoke([&]{ gpu->triangle(g,a,b,c); });
    }
    void sprite(const Ge &g, const Vertex &a, const Vertex &b) override {
        invoke([&]{ gpu->sprite(g,a,b); });
    }
    void block_transfer(unsigned s, unsigned ss, unsigned sx, unsigned sy,
                        unsigned d, unsigned ds, unsigned dx, unsigned dy,
                        unsigned w, unsigned h, unsigned b) override {
        invoke([&]{ gpu->block_transfer(s,ss,sx,sy,d,ds,dx,dy,w,h,b); });
    }
    void flush() override { invoke([&]{ gpu->flush(); }); }
    void present(unsigned a, unsigned s, unsigned f) override { invoke([&]{ gpu->present(a,s,f); }); }
};

}
void pocket_install_gpu(mhf::psp::Runtime &rt) {
    // Software mode can be selected without rebuilding or replacing assets.
    if (FILE *f = std::fopen("ux0:data/pocketprobe/software.flag", "rb")) {
        std::fclose(f); std::fprintf(stderr, "[gpu] software.flag present: using software renderer\n"); return;
    }
    bool shader_compiler = false;
    for (const char *path : {"ur0:data/libshacccg.suprx", "ur0:data/external/libshacccg.suprx"}) {
        if (FILE *f = std::fopen(path, "rb")) { std::fclose(f); shader_compiler = true; break; }
    }
    if (!shader_compiler) {
        std::fprintf(stderr, "[gpu] libshacccg.suprx absent: software fallback (GPU NOT active)\n"); return;
    }
    set_gpu_backend_factory(rt, [&rt]() -> std::unique_ptr<gpu::Backend> {
        auto backend = std::make_unique<VitaGpu>(rt);
        if (!backend->init()) {
            std::fprintf(stderr, "[gpu] initialization failed; software fallback\n"); return nullptr;
        }
        return std::make_unique<GuardedGpu>(std::move(backend));
    });
    std::fprintf(stderr, "[gpu] factory installed; initialization pending on GE worker\n");
}
