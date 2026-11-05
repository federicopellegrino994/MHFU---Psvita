// Experimental Vita hardware GE backend. Rasterization uses vitaGL/GXM, not the CPU.
// This is a compatibility bring-up, NOT a complete PSP GE implementation.
#include "vita_gpu.hpp"
#include "mhf/psp/host.hpp"
#include "ge.hpp"
#include "rigid_gpu_math.hpp"
#include "rigid_visibility_cache.hpp"
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
    std::chrono::steady_clock::time_point start{};
    bool running;
    bool sampled;
    unsigned multiplier;
    explicit Cost(std::uint64_t &v, bool sample = true, unsigned scale = 1)
        : total(v), running(false), sampled(false), multiplier(scale) {
        (void)sample; // v10: legacy fine timers disabled; sparse probe replaces them
        if (running) start = std::chrono::steady_clock::now();
    }
    bool enabled() const { return multiplier != 0; }
    void pause() {
        if (!running) return;
        total += std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - start).count() * multiplier;
        running = false;
    }
    void resume() { if (sampled) { start = std::chrono::steady_clock::now(); running = true; } }
    ~Cost() { pause(); }
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
    bool batch_model = false;
    pocket::rigid::Matrix batch_matrix{};
    std::vector<GVertex> rigid_scratch;
    pocket::rigid::Transform rigid_transform;
    pocket::rigid::VisibilityCache visibility_cache;
    std::uint64_t rigid_generation = ~std::uint64_t(0);
    bool rigid_transform_valid = false, rigid_enabled = true, rigid_cull = true;
    bool u8_enabled = true;
    bool pmo_group_gpu=true;
    unsigned pmo_gpu_groups=0,pmo_gpu_boundary=0,pmo_gpu_culled=0;
    struct U8Profile {unsigned attempts{}, draws{}, culled{};} u8_profile;
    struct RigidProfile { unsigned attempts{}, draws{}, vertices{}, state{}, memory{}, clip{}, matrix_splits{}, batches{}, culled_draws{}, culled_vertices{}, outside_near{}, outside_guard{}, boundary{}; } rigid_profile;
    std::array<unsigned, 256> rejected_states{};
    std::vector<std::uint32_t> pixels;
    std::size_t texture_bytes = 0;
    std::uint64_t serial = 0;
    unsigned frames = 0, texture_width = 1, texture_height = 1;
    struct Profile {
        std::uint64_t hash{}, decode{}, upload{}, setup{}, submit{}, flush{}, swap{};
        unsigned requests{}, hits{}, misses{}, draws{}, triangles{}, reused{}, hash_checks{}, hash_reuses{}, invalidations{}, fast_states{};
    } profile;
    unsigned target_offset = 0;
    struct StateKey {
        std::array<std::uint32_t, 64> regs{};
        std::uint64_t hash = 0;
        unsigned resource = 0; // 0 untextured, 1 guest texture, 2 live GPU target
        bool operator==(const StateKey &) const = default;
    } last_state;
    bool state_valid = false;
    std::uint64_t last_render_generation = ~std::uint64_t(0);
    std::uint64_t last_texture_generation = ~std::uint64_t(0);
    std::uint64_t planned_hash = 0;
    // POCKET_TEXTURE_VALIDATION_V4: retain a content fingerprint only during
    // an explicit GE cache epoch. Also expire on every flush/stall/list/present.
    struct ValidationKey {
        std::array<unsigned, 8> description{};
        std::uint64_t palette = 0;
        bool operator<(const ValidationKey &b) const {
            return description < b.description || (description == b.description && palette < b.palette);
        }
    };
    std::map<ValidationKey, std::uint64_t> validations;
    std::uint64_t validation_epoch = ~std::uint64_t(0);
    bool strict_validation = false;
    void clear_validations() { validations.clear(); validation_epoch = ~std::uint64_t(0); }
    std::uint64_t validated_hash(const Ge &ge, const Ge::TextureInfo &info) {
        auto hash_measure=ge.render_probe().scope(pocket::RenderProbe::Hash,8);
        Cost cost(profile.hash, (serial & 63) == 0, 64);
        if (!strict_validation) {
            const auto epoch = ge.texture_cache_generation();
            if (epoch != validation_epoch) {
                validations.clear(); validation_epoch = epoch; ++profile.invalidations;
            }
            ValidationKey key{{info.address,info.stride,info.format,info.width,info.height,
                               info.bytes,info.swizzled ? 1u : 0u,ge.reg(0xc5)},
                              info.format >= 4 && info.format <= 7 ? ge.clut_generation() : 0};
            const auto found = validations.find(key);
            if (found != validations.end()) { ++profile.hash_reuses; return found->second; }
            ++profile.hash_checks;
            const auto hash = ge.texture_hash();
            if (validations.size() >= 512) validations.clear();
            validations.emplace(key,hash);
            return hash;
        }
        ++profile.hash_checks;
        return ge.texture_hash();
    }
    StateKey state_key(const Ge &ge) {
        static constexpr unsigned regs[] = {
            0x12,0x1e,0x1f,0x21,0x22,0x23,0x24,0x27,0x48,0x49,0x4a,0x4b,0x50,
            0x9c,0x9d,0x9e,0x9f,0xa0,0xa8,0xb0,0xb1,0xb8,0xc0,0xc2,0xc3,
            0xc5,0xc6,0xc7,0xc9,0xca,0xcf,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
            0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe0,0xe1,0xe6,0xe7,0xe8,0xe9
        };
        StateKey key;
        for (unsigned i = 0; i < sizeof(regs)/sizeof(regs[0]); ++i) key.regs[i] = ge.reg(regs[i]);
        if (!(ge.reg(0xd3) & 1) && (ge.reg(0x1e) & 1)) {
            const auto info = ge.texture_info();
            const auto rt = targets.find(offset(info.address));
            if ((info.address & 0x1f000000u) == 0x04000000u && rt != targets.end() &&
                info.format <= 3 && rt->first != (ge.reg(0x9c) & 0x1ffff0)) {
                key.resource = 2; // sample the live GPU texture, not stale guest bytes
            } else {
                key.resource = 1;
                ++profile.requests;
                key.hash = validated_hash(ge, info);
            }
        }
        return key;
    }
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
        const std::uint64_t hash = planned_hash;
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
        { auto decode_measure=ge.render_probe().scope(pocket::RenderProbe::Decode,0); ge.decode_texture(pixels.data()); }
        {
            auto upload_measure=ge.render_probe().scope(pocket::RenderProbe::Upload,0);
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
        auto state_measure=ge.render_probe().scope(pocket::RenderProbe::Prepare);
        const auto render_generation = ge.render_state_generation();
        const auto texture_generation = ge.texture_cache_generation();
        // POCKET_STATE_FAST_V5: same immutable GE draw state and texture epoch.
        // Bypass descriptor construction, map lookup and state-array comparison.
        // Strict mode still checks the texture contents before every guest draw.
        if (!strict_validation && state_valid &&
            render_generation == last_render_generation &&
            texture_generation == last_texture_generation) {
            if (last_state.resource == 1) {
                ++profile.requests; ++profile.hits; ++profile.hash_reuses;
            }
            ++profile.reused; ++profile.fast_states;
            prepared = true;
            return;
        }
        Cost setup_cost(profile.setup, (serial & 63) == 0, 64);
        const StateKey next = state_key(ge);
        if (state_valid && next == last_state) {
            last_render_generation = render_generation;
            last_texture_generation = texture_generation;
            if (next.resource == 1) {
                ++profile.hits;
                auto cached = textures.find(next.hash);
                if (cached != textures.end()) cached->second.stamp = serial;
            }
            ++profile.reused;
            prepared = true;
            return;
        }
        // Pending vertices must use the old state and textures. Flush BEFORE
        // changing bindings, allocating targets, uploading or evicting textures.
        setup_cost.pause();
        draw_batch();
        setup_cost.resume();
        planned_hash = next.hash;
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
        last_state = next;
        last_render_generation = render_generation;
        last_texture_generation = texture_generation;
        state_valid = true;
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
    // Keep CPU clip-space and GPU model-space batches separate. Matrix changes
    // must submit OLD vertices before changing the matrix attached to a batch.
    void begin_batch(bool model, const pocket::rigid::Matrix *matrix = nullptr) {
        if (!batch.empty() && (model != batch_model || (model && *matrix != batch_matrix))) {
            if (model && batch_model) ++rigid_profile.matrix_splits;
            draw_batch();
        }
        batch_model = model;
        if (model) batch_matrix = *matrix;
    }
    void draw_batch() {
        if (batch.empty()) return;
        Cost submit_cost(profile.submit, (profile.draws & 63) == 0, 64);
        ++profile.draws;
        profile.triangles += static_cast<unsigned>(batch.size() / 3);
        glEnableClientState(GL_VERTEX_ARRAY); glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(4, GL_FLOAT, sizeof(GVertex), batch[0].position);
        glColorPointer(4, GL_FLOAT, sizeof(GVertex), batch[0].color);
        glTexCoordPointer(2, GL_FLOAT, sizeof(GVertex), batch[0].uv);
        if (batch_model) {
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glMatrixMode(GL_PROJECTION); glLoadMatrixf(batch_matrix.data());
            ++rigid_profile.batches;
        }
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.size()));
        if (batch_model) {
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
        }
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
        std::fprintf(stderr, "[gpu] initializing vitaGL on GE worker; native rasterization test v5-state-generation\n");
        // vitaGL returns whether the requested display resolution was reduced,
        // NOT an initialization success flag (vgl.c, return res_fallback).
        const GLboolean reduced = vglInitExtended(1024 * 1024, 960, 544, 8 * 1024 * 1024, SCE_GXM_MULTISAMPLE_NONE);
        std::fprintf(stderr, "[gpu] vitaGL initialized; resolution fallback=%u\n", unsigned(reduced));
        if (FILE *f = std::fopen("ux0:data/pocketprobe/strict-textures.flag", "rb")) {
            std::fclose(f); strict_validation = true;
        }
        std::fprintf(stderr, "[gpu] texture validation: %s; fine timers replaced by render-v10\n",
                     strict_validation ? "strict every draw" : "GE cache epochs + list boundaries");
        if (FILE *f=std::fopen("ux0:data/pocketprobe/no-rigid-gpu.flag","rb")) {
            std::fclose(f);rigid_enabled=false;
        }
        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-rigid-cull.flag","rb")){
            std::fclose(f);rigid_cull=false;
        }
        std::fprintf(stderr,"[gpu-rigid-cull] v9 %s; whole-draw PSP near/guard rejection only\n",rigid_cull?"enabled":"disabled by flag");
        std::fprintf(stderr,"[render-v10] sparse profiling installed; old fine timers disabled; no-sparse-profile.flag disables new probe\n");
        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-vertex-cache.flag","rb")){
            std::fclose(f);visibility_cache.enabled(false);
        }
        std::fprintf(stderr,"[gpu-vertex-cache] v11 %s; exact XYZ bytes + clip/bound/viewport; 4096 entries; colors/UV always fresh\n",visibility_cache.enabled()?"enabled":"disabled by flag");
        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-u8-gpu.flag","rb")){
            std::fclose(f);u8_enabled=false;
        }
        std::fprintf(stderr,"[gpu-u8] v12 %s; VTYPE 00091A; byte indices; same XYZ/color/UV layout as 00111A\n",u8_enabled?"enabled":"disabled by flag");
        if(FILE*f=std::fopen("ux0:data/pocketprobe/no-pmo-group-gpu.flag","rb")){std::fclose(f);pmo_group_gpu=false;}
        std::fprintf(stderr,"[pmo-native] live-v1 %s; GPU groups %s; original command stream + whole-group decode\n",
            pocket::pmo::live_enabled()?"enabled":"disabled by flag",pmo_group_gpu?"enabled":"disabled by flag");
        rigid_scratch.reserve(256);
        std::fprintf(stderr,"[gpu-rigid] v12 %s; VTYPE 00111A + optional 00091A; GPU matrix transform with conservative CPU clipping checks\n",rigid_enabled?"enabled":"disabled by flag");
        vglWaitVblankStart(GL_FALSE);
        plain_state();
        std::fprintf(stderr, "[gpu] hardware backend active; guest 480x272, display 960x544\n");
        return true;
    }
    bool supports_native_pmo() const override {return true;}
    bool draw_native_group(const Ge &ge,const pocket::pmo::Geometry &p) override {
        if(!pmo_group_gpu || !rigid_enabled || (p.vertex_type==0x091au&&!u8_enabled))return false;
        if((ge.reg(0x17)&1u)||(ge.reg(0xd3)&1u)||(ge.reg(0xc0)&3u))return false;
        const auto generation=ge.rigid_transform_generation();
        if(!rigid_transform_valid || generation!=rigid_generation) {
            pocket::rigid::Parameters parameters;
            parameters.world=ge.world_matrix().m;parameters.view=ge.view_matrix().m;parameters.projection=ge.proj_matrix().m;
            parameters.viewport={gpu::float24(ge.reg(0x42)),gpu::float24(ge.reg(0x43)),gpu::float24(ge.reg(0x44)),
                gpu::float24(ge.reg(0x45)),gpu::float24(ge.reg(0x46)),gpu::float24(ge.reg(0x47)),
                float(ge.reg(0x4c)&65535)/16,float(ge.reg(0x4d)&65535)/16};
            rigid_transform_valid=rigid_transform.build(parameters);rigid_generation=generation;
            if(rigid_transform_valid)visibility_cache.set_transform(rigid_transform);
        }
        if(!rigid_transform_valid)return false;
        const float us=gpu::float24(ge.reg(0x48)),vs=gpu::float24(ge.reg(0x49));
        const float uo=gpu::float24(ge.reg(0x4a)),vo=gpu::float24(ge.reg(0x4b));
        if(!std::isfinite(us)||!std::isfinite(vs)||!std::isfinite(uo)||!std::isfinite(vo))return false;
        bool box_safe=true;
        for(unsigned n=0;n<8;++n)
            if(!rigid_transform.accepts((n&1)?p.high[0]:p.low[0],(n&2)?p.high[1]:p.low[1],(n&4)?p.high[2]:p.low[2])) {box_safe=false;break;}
        if(!box_safe) {
            bool all_safe=true,all_outside=true;
            for(const auto &v:p.vertices) {
                // Normalized int16 positions retain every original bit.
                // Reconstruct the six SNAPSHOT bytes for the existing cache;
                // do not reread mutable guest memory after validation.
                const std::int16_t xyz[]={static_cast<std::int16_t>(v.position[0]*32768.0f),
                    static_cast<std::int16_t>(v.position[1]*32768.0f),static_cast<std::int16_t>(v.position[2]*32768.0f)};
                const auto visible=visibility_cache.classify(reinterpret_cast<const std::uint8_t*>(xyz));
                using V=pocket::rigid::Transform::Visibility;
                if(visible==V::Safe)all_outside=false;
                else if(visible==V::Boundary){++pmo_gpu_boundary;return false;}
                else all_safe=false;
                if(!all_safe&&!all_outside){++pmo_gpu_boundary;return false;}
            }
            if(all_outside) {
                if(!rigid_cull)return false;
                // As in v9: each triangle either has no near-inside vertices
                // or a near-inside vertex outside the PSP guard band.
                ++pmo_gpu_culled;return true;
            }
        }
        prepared=false;++serial;prepare(ge);begin_batch(true,&rigid_transform.gpu);
        for(unsigned i=0;i+2<p.triangles.size();i+=3) {
            const auto &last=p.vertices[p.triangles[i+2]];
            for(unsigned n=0;n<3;++n) {
                const auto &v=p.vertices[p.triangles[i+n]];GVertex out;
                std::copy_n(v.position,4,out.position);std::copy_n(flat?last.color:v.color,4,out.color);
                out.uv[0]=v.uv[0]*us+uo;out.uv[1]=v.uv[1]*vs+vo;
                if(target_texture){out.uv[0]*=float(texture_width)/Side;out.uv[1]=1-out.uv[1]*(float(texture_height)/Side);}
                batch.push_back(out);
            }
            if(batch.size()>=12288)draw_batch();
        }
        ++pmo_gpu_groups;return true;
    }
    bool draw_rigid(const Ge &ge, unsigned type, unsigned count, unsigned vaddr, unsigned iaddr) override {
        if (!rigid_enabled) return false;
        ++rigid_profile.attempts;
        const auto vt=ge.reg(0x12);
        const bool u8=vt==0x00091Au;
        if(u8)++u8_profile.attempts;
        const unsigned index_format = (vt >> 11u) & 3u;
        const unsigned color_format = (vt >> 2u) & 7u;
        const bool simple_layout = (vt & ((3u << 9u) | (7u << 18u) | (1u << 23u))) == 0 &&
            ((vt >> 7u) & 3u) != 0 && index_format < 3 &&
            (color_format == 0 || color_format >= 4);
        if (!simple_layout || (u8 && !u8_enabled) || type < 3 || type > 5 || count < 3 || count > 4096 ||
            (ge.reg(0x17)&1) || (ge.reg(0xd3)&1) || (ge.reg(0xc0)&3)) {
            const unsigned reasons =
                ((vt & (1u<<23u)) ? 1u : 0u) |
                ((ge.reg(0x17)&1u) ? 2u : 0u) |
                ((vt & (3u<<9u)) ? 4u : 0u) |
                ((vt & (7u<<18u)) ? 8u : 0u) |
                ((type < 3 || type > 5) ? 16u : 0u) |
                ((ge.reg(0xd3)&1u) ? 32u : 0u) |
                ((ge.reg(0xc0)&3u) ? 64u : 0u) |
                ((!simple_layout || count < 3 || count > 4096 || (u8 && !u8_enabled)) ? 128u : 0u);
            ++rejected_states[reasons];
            ++rigid_profile.state; return false;
        }
        auto rigid_measure=ge.render_probe().scope(pocket::RenderProbe::Rigid);
        const auto generation = ge.rigid_transform_generation();
        if (!rigid_transform_valid || generation != rigid_generation) {
            auto matrix_measure=ge.render_probe().scope(pocket::RenderProbe::MatrixBuild,8);
            pocket::rigid::Parameters p;
            p.world=ge.world_matrix().m; p.view=ge.view_matrix().m; p.projection=ge.proj_matrix().m;
            p.viewport={gpu::float24(ge.reg(0x42)),gpu::float24(ge.reg(0x43)),gpu::float24(ge.reg(0x44)),
                gpu::float24(ge.reg(0x45)),gpu::float24(ge.reg(0x46)),gpu::float24(ge.reg(0x47)),
                float(ge.reg(0x4c)&65535)/16,float(ge.reg(0x4d)&65535)/16};
            rigid_transform_valid=rigid_transform.build(p);rigid_generation=generation;
            if(rigid_transform_valid)visibility_cache.set_transform(rigid_transform);
        }
        if (!rigid_transform_valid) {++rigid_profile.clip;return false;}
        const unsigned index_bytes = index_format == 1 ? 1u : 2u;
        const auto *indices = index_format ? ge.guest_bytes(iaddr,count*index_bytes) : nullptr;
        if (index_format && !indices) {++rigid_profile.memory;return false;}
        const auto index_at=[&](unsigned n)->unsigned{
            if (!index_format) return n;
            if (index_format == 1) return indices[n];
            std::uint16_t v;std::memcpy(&v,indices+n*2,2);return v;
        };
        unsigned max_index=0;
        for(unsigned i=0;i<count;++i)max_index=std::max(max_index,unsigned(index_at(i)));
        const unsigned stride = ge.vertex_size();
        const auto *vertices=ge.guest_bytes(vaddr,(max_index+1)*stride);
        if(!vertices){++rigid_profile.memory;return false;}
        const float us=gpu::float24(ge.reg(0x48)),vs=gpu::float24(ge.reg(0x49));
        const float uo=gpu::float24(ge.reg(0x4a)),vo=gpu::float24(ge.reg(0x4b));
        if(!std::isfinite(us)||!std::isfinite(vs)||!std::isfinite(uo)||!std::isfinite(vo)){++rigid_profile.state;return false;}
        auto validate_measure=ge.render_probe().scope(pocket::RenderProbe::Validate);
        bool all_safe=true,all_outside=true;
        rigid_scratch.resize(count);
        for(unsigned i=0;i<count;++i){
            const auto decoded = ge.decode_vertex(vaddr + index_at(i) * stride);
            auto &out = rigid_scratch[i];
            std::copy_n(decoded.pos, 3, out.position);
            out.position[3] = 1.0f;
            const unsigned material = ge.reg(0x55) | (ge.reg(0x58) << 24u);
            for (unsigned c = 0; c < 4; ++c)
                out.color[c] = (decoded.has_color ? decoded.color[c] : float((material >> (8*c)) & 255u)) * (1.0f/255.0f);
            out.uv[0] = decoded.uv[0]*us+uo;
            out.uv[1] = decoded.uv[1]*vs+vo;
            using Visibility=pocket::rigid::Transform::Visibility;
            const auto visibility = (vt == 0x111au || vt == 0x91au)
                ? visibility_cache.classify(vertices + index_at(i)*12u + 6)
                : rigid_transform.classify(decoded.pos[0], decoded.pos[1], decoded.pos[2]);
            if(visibility==Visibility::Safe) all_outside=false;
            else {
                all_safe=false;
                if(visibility==Visibility::OutsideNear)++rigid_profile.outside_near;
                else if(visibility==Visibility::OutsideGuard)++rigid_profile.outside_guard;
                else {++rigid_profile.boundary;all_outside=false;}
                if(!rigid_cull){++rigid_profile.clip;return false;}
            }
        }
        if(!all_safe){
            if(all_outside){
                // Every vertex is certainly outside the near plane or, while
                // near-inside, outside the PSP guard band. Thus every triangle
                // either has a guard-band rejection or has no near-inside
                // vertices. The reference Ge::draw_triangle emits nothing.
                if(u8)++u8_profile.culled;
                ++rigid_profile.culled_draws;rigid_profile.culled_vertices+=count;
                return true;
            }
            ++rigid_profile.clip;return false;
        }
        validate_measure.stop();
        auto submit_measure=ge.render_probe().scope(pocket::RenderProbe::Submit);
        // No GL or batch mutation occurred before ALL vertices were validated.
        prepared=false;++serial;prepare(ge);
        begin_batch(true,&rigid_transform.gpu);
        const auto append=[&](const GVertex &v,const GVertex &color){
            GVertex out=v;
            if(flat)std::copy_n(color.color,4,out.color);
            if(target_texture){out.uv[0]*=float(texture_width)/Side;out.uv[1]=1-out.uv[1]*(float(texture_height)/Side);}
            batch.push_back(out);
        };
        const auto tri=[&](unsigned a,unsigned b,unsigned c){
            append(rigid_scratch[a],rigid_scratch[c]);append(rigid_scratch[b],rigid_scratch[c]);append(rigid_scratch[c],rigid_scratch[c]);
            if(batch.size()>=12288)draw_batch();
        };
        if(type==3)for(unsigned i=0;i+2<count;i+=3)tri(i,i+1,i+2);
        if(type==4)for(unsigned i=0;i+2<count;++i){if(i&1)tri(i+1,i,i+2);else tri(i,i+1,i+2);}
        if(type==5)for(unsigned i=1;i+1<count;++i)tri(0,i,i+1);
        if(u8)++u8_profile.draws;
        ++rigid_profile.draws;rigid_profile.vertices+=count;
        return true;
    }
    void draw_started(const gpu::DrawInfo &) override { prepared = false; ++serial; }
    void triangle(const Ge &ge, const Vertex &a, const Vertex &b, const Vertex &c) override {
        if (!prepared) prepare(ge);
        if (batch_model) begin_batch(false);
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
    void flush() override { draw_batch(); { Cost cost(profile.flush); glFlush(); } prepared = false; state_valid = false; clear_validations(); }
    void block_transfer(unsigned, unsigned, unsigned, unsigned, unsigned dst, unsigned, unsigned, unsigned,
                        unsigned, unsigned, unsigned) override {
        draw_batch(); prepared = false; state_valid = false; clear_validations();
        // Source GPU data is not mirrored in guest VRAM in this first bring-up.
        // Explicit diagnostic rather than claiming readback/copy compatibility.
        warn(128, "block transfers use guest memory; GPU readback not implemented");
        const auto i = targets.find(offset(dst));
        if (i != targets.end()) erase_target(i);
    }
    void present(unsigned address, unsigned stride, unsigned format) override {
        draw_batch(); prepared = false; state_valid = false; clear_validations();
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
            std::fprintf(stderr, "[gpu-cost-legacy-disabled] use render-v10-time; old counters: setup_total=%.2f hash=%.2f decode=%.2f upload=%.2f submit=%.2f flush=%.2f swap=%.2f\n",
                profile.setup * per_frame, profile.hash * per_frame, profile.decode * per_frame,
                profile.upload * per_frame, profile.submit * per_frame, profile.flush * per_frame, profile.swap * per_frame);
            std::fprintf(stderr, "[gpu-count] period: requests=%u hits=%u misses=%u draws=%u triangles=%u reused_states=%u\n",
                profile.requests, profile.hits, profile.misses, profile.draws, profile.triangles, profile.reused);
            std::fprintf(stderr, "[gpu-validation] period: full_checks=%u reuses=%u epochs=%u fast_states=%u\n",
                         profile.hash_checks, profile.hash_reuses, profile.invalidations, profile.fast_states);
            std::fprintf(stderr,"[gpu-rigid] period: attempts=%u draws=%u vertices=%u fallback_state=%u fallback_memory=%u fallback_clip=%u batches=%u matrix_splits=%u\n",
                rigid_profile.attempts,rigid_profile.draws,rigid_profile.vertices,rigid_profile.state,rigid_profile.memory,rigid_profile.clip,rigid_profile.batches,rigid_profile.matrix_splits);
            std::fprintf(stderr,"[gpu-rigid-cull] period: culled_draws=%u culled_vertices=%u outside_near=%u outside_guard=%u boundary_vertices=%u\n",
                rigid_profile.culled_draws,rigid_profile.culled_vertices,rigid_profile.outside_near,rigid_profile.outside_guard,rigid_profile.boundary);
            std::fprintf(stderr,"[gpu-vertex-cache] period: hits=%u misses=%u\n",
                static_cast<unsigned>(visibility_cache.hits()),static_cast<unsigned>(visibility_cache.misses()));
            visibility_cache.reset_counts();
            std::fprintf(stderr,"[gpu-u8] period: attempts=%u draws=%u culled=%u fallback=%u\n",
                u8_profile.attempts,u8_profile.draws,u8_profile.culled,
                u8_profile.attempts-u8_profile.draws-u8_profile.culled);
            std::fprintf(stderr,"[pmo-native-gpu] period: groups=%u culled=%u boundary_to_cpu=%u\n",pmo_gpu_groups,pmo_gpu_culled,pmo_gpu_boundary);
            pmo_gpu_groups=pmo_gpu_culled=pmo_gpu_boundary=0;
            u8_profile={};
            for (unsigned mask = 0; mask < rejected_states.size(); ++mask)
                if (rejected_states[mask])
                    std::fprintf(stderr, "[gpu-reject] mask=%02X count=%u\n", mask, rejected_states[mask]);
            rejected_states.fill(0);
            rigid_profile={};
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
    bool supports_native_pmo() const override {return gpu->supports_native_pmo();}
    bool draw_native_group(const Ge &g,const pocket::pmo::Geometry&p) override {
        bool handled=false;invoke([&]{handled=gpu->draw_native_group(g,p);});return handled;
    }
    bool draw_rigid(const Ge &g,unsigned type,unsigned count,std::uint32_t va,std::uint32_t ia) override {
        bool handled=false;invoke([&]{handled=gpu->draw_rigid(g,type,count,va,ia);});return handled;
    }
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
