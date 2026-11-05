// Experimental Vita hardware GE backend. Rasterization uses vitaGL/GXM, not the CPU.
// This is a compatibility bring-up, NOT a complete PSP GE implementation.
#include "vita_gpu.hpp"
#include "mhf/psp/host.hpp"
#include "ge.hpp"
#include "rigid_gpu_math.hpp"
#include "rigid_visibility_cache.hpp"
#include "pocket_pixel.hpp"
#include "pocket_phase_probe.hpp" // POCKET_PHASE_PROBE_V9
#include "skin_gpu.hpp"       // POCKET_GPU_SKIN_V13
#include "pocket_clip.hpp"     // POCKET_PMO_SPLIT_V15
#include <vitaGL.h>
#include <unordered_map> // POCKET_GPU_SKIN_V13K
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
#include <unordered_map>

namespace {
using namespace mhf::psp;
using gpu::Ge;
using gpu::Vertex;
#ifndef GL_CG_VERTEX_SHADER_EXT
#define GL_CG_VERTEX_SHADER_EXT 0x890E
#define GL_CG_FRAGMENT_SHADER_EXT 0x890F
#endif
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
    unsigned pmo_split_groups=0,pmo_split_cpu=0,pmo_split_gpu=0; bool pmo_split=true; // POCKET_PMO_SPLIT_V15
    std::vector<std::uint8_t> pmo_vis; std::vector<GVertex> pmo_clipped;
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
        // POCKET_STATE_KEY_V7: vertices arrive already converted to the single GVertex layout. Of VTYPE only the
        // through (2D) bit changes GL state; UV scale/offset are already applied to the vertices. Keying on them
        // flushed the batch and rebuilt every GL state whenever two meshes differed only in bone count.
        key.regs[0] = ge.reg(0x12) & (1u << 23);
        key.regs[8] = key.regs[9] = key.regs[10] = key.regs[11] = 0;
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
    // POCKET_GPU_SKIN_V13: skinning + PSP lighting in a vertex shader for the formats pocket::skin::format_for() takes.
    // Enabled with ux0:data/pocketprobe/gpu-skin.flag. The program is compiled the first time it is needed; if that
    // fails the path stays off and the CPU path draws everything, as before.
    bool skin_enabled = false, skin_tried = false, skin_ready = false, skin_bound = false;
    GLuint skin_program = 0;
    GLint u_bones = -1, u_world = -1, u_mvp = -1, u_uvt = -1, u_lpos = -1, u_ldir = -1, u_latt = -1,
          u_lspot = -1, u_lamb = -1, u_ldif = -1, u_lspe = -1, u_mamb = -1, u_mdif = -1, u_mspe = -1, u_base = -1,
          u_params = -1, u_near = -1, u_tex = -1, u_env = -1, u_alpha = -1, u_sampler = -1;
    pocket::skin::Uniforms skin_u;
    std::uint64_t skin_light_gen = ~0ull, skin_xform_gen = ~0ull;
    std::vector<pocket::skin::Vertex> skin_vertices;
    std::vector<std::uint16_t> skin_indices;
    struct { unsigned draws{}, vertices{}, format{}, prim{}, texgen{}, clear{}, memory{}, transform{}, range{}, rigid{}, rigid_cpu{}, rigid_culled{}; } skin_profile;
    bool skin_check = true; unsigned skin_check_logged = 0; // POCKET_GPU_SKIN_V13E self-check (no-skin-check.flag: off)
    struct { unsigned draws{}, vertices{}, bad_pos{}, bad_gl{}, bad_color{}, bad_uv{}; } skin_check_count;
    struct CensusEntry { unsigned draws{}, vertices{}; std::uint64_t fix{}; unsigned env{}, material{}; };
    std::unordered_map<std::uint64_t, CensusEntry> skin_census; // POCKET_GPU_SKIN_V13K
    bool skin_rigid = true;
    // POCKET_GPU_SKIN_V13G: skin-debug.flag cycles 8 variants of the GPU path, 8 seconds each, and draws as many white
    // squares in the top-left corner as the variant number + 1, so a video shows which variant fixes the village.
    bool skin_debug = false; int skin_debug_last = -1;
    std::chrono::steady_clock::time_point skin_debug_start{};
    int skin_debug_mode() {
        if (!skin_debug) return -1;
        const auto now = std::chrono::steady_clock::now();
        if (skin_debug_last < 0 && skin_debug_start == std::chrono::steady_clock::time_point{}) skin_debug_start = now;
        // POCKET_GPU_SKIN_V13H: 5 variants x 10 s.
        const int mode = int(std::chrono::duration<float>(now - skin_debug_start).count() / 10.0f) % 5;
        if (mode != skin_debug_last) {
            static const char *names[5] = {"0 plain shader normal", "1 plain shader, colour doubling ignored",
                "2 plain shader, texture off (vertex colours only)", "3 plain shader, texture only (no vertex colour)",
                "4 GPU skin OFF (characters on the CPU path), plain shader normal"}; // POCKET_GPU_SKIN_V13J
            std::fprintf(stderr, "[skin-debug] variant %s (%d white squares)\n", names[mode], mode + 1); std::fflush(stderr);
            skin_debug_last = mode;
        }
        return mode;
    }
    void skin_debug_marker() {
        const int mode = skin_debug_last;
        if (!skin_debug || mode < 0) return;
        if (plain_active()) { // POCKET_GPU_SKIN_V13J: squares through the plain program
            GVertex q[30]; int n = 0;
            for (int k = 0; k <= mode && k < 5; ++k) {
                const float x1 = 0.97f - k * 0.10f, x0 = x1 - 0.07f, y0 = -0.82f, y1 = -0.95f;
                const float c[6][2] = {{x0, y0}, {x1, y0}, {x0, y1}, {x1, y0}, {x1, y1}, {x0, y1}};
                for (auto &xy : c) q[n++] = GVertex{{xy[0], xy[1], 0, 1}, {1, 1, 1, 1}, {0, 0}};
            }
            frag_tex[0] = 0; frag_alpha[0] = 0;
            plain_draw(GL_TRIANGLES, q, n, nullptr, false);
            return;
        }
        glDisable(GL_TEXTURE_2D); glColor4f(1, 1, 1, 1);
        glBegin(GL_TRIANGLES);
        for (int k = 0; k <= mode; ++k) { // POCKET_GPU_SKIN_V13H: bottom-right, clear of the CPU/FPS overlay
            const float x1 = 0.97f - k * 0.10f, x0 = x1 - 0.07f, y0 = -0.82f, y1 = -0.95f;
            glVertex2f(x0, y0); glVertex2f(x1, y0); glVertex2f(x0, y1);
            glVertex2f(x1, y0); glVertex2f(x1, y1); glVertex2f(x0, y1);
        }
        glEnd();
    } // POCKET_GPU_SKIN_V13D: models without weights also take the GPU path (skin-weighted-only.flag: no)
    // POCKET_GPU_SKIN_V13L: one custom program for skinned models AND every other draw (uMode selects).
    // v13i used two programs; switching between them left the plain draws washed out (village under a white
    // veil while the characters were right, and fine with the skin off). Now nothing is ever switched.
    int bound_program = 0;          // 0 fixed function, 1 the program in skin mode, 2 the program in plain mode
    // POCKET_GPU_BATCH_V16: pending skinned draws (see draw_skinned)
    static constexpr unsigned kSkinSlots = 8;
    bool skin_batching = true;      // no-skin-batch.flag: one GL draw per PSP draw (v15)
    std::vector<pocket::skin::Vertex> sb_vertices; std::vector<std::uint16_t> sb_indices;
    GLfloat sb_bones[kSkinSlots * 24][4]{}, sb_uvs[kSkinSlots][4]{}, sb_uvt[4]{}, sb_tex[4]{}, sb_env[4]{}, sb_alpha[4]{};
    unsigned sb_slots = 0, skin_gl_draws = 0;
    GLint u_uvs = -1;
    void flush_skin() {
        if (!sb_slots) return;
        use_skin_program();
        glUniform4fv(u_bones, GLsizei(sb_slots * 24), &sb_bones[0][0]);
        glUniform4fv(u_uvs, GLsizei(sb_slots), &sb_uvs[0][0]);
        glUniform4fv(u_uvt, 1, sb_uvt);
        glUniform4fv(u_tex, 1, sb_tex); glUniform4fv(u_env, 1, sb_env); glUniform4fv(u_alpha, 1, sb_alpha);
        const auto *base = reinterpret_cast<const std::uint8_t *>(sb_vertices.data());
        constexpr GLsizei stride = sizeof(pocket::skin::Vertex);
        glVertexAttribPointer(0, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, base + 0);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, base + 4);
        glVertexAttribPointer(2, 2, GL_UNSIGNED_SHORT, GL_FALSE, stride, base + 8);
        glVertexAttribPointer(3, 4, GL_BYTE, GL_FALSE, stride, base + 12);   // normal + slot
        glVertexAttribPointer(4, 3, GL_SHORT, GL_FALSE, stride, base + 16);
        glVertexAttribPointer(5, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, base + 0); // unused in skin mode
        glVertexAttribPointer(6, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, base + 4);
        if (!sb_indices.empty()) {
            pocket::phase::GeScope submit_phase(pocket::phase::GeSubmit);
            glDrawElements(GL_TRIANGLES, GLsizei(sb_indices.size()), GL_UNSIGNED_SHORT, sb_indices.data());
        }
        ++skin_gl_draws;
        sb_vertices.clear(); sb_indices.clear(); sb_slots = 0;
    }
    bool plain_enabled = true;      // no-plain-shader.flag: fixed-function batches (v13h behaviour)
    GLuint plain_program = 0, white_texture = 0;
    GLint u_mode = -1;
    GLfloat frag_tex[4] = {0, 0, 0, 1}, frag_env[4] = {0, 0, 0, 1}, frag_alpha[4] = {0, 0, 0, 0}; // of the batch
    bool plain_active() const { return skin_ready && plain_program != 0; }
    void ffp_program() { // back to vitaGL's fixed-function pipeline (only without the plain mode)
        if (bound_program == 0) return;
        glUseProgram(0);
        for (GLuint i = 0; i < 7; ++i) glDisableVertexAttribArray(i);
        bound_program = 0; skin_bound = false;
    }
    void use_skin_program() {
        if (bound_program == 1) return;
        if (bound_program == 0) glUseProgram(skin_program);
        static const GLfloat skin_mode[4] = {0, 0, 0, 0};
        glUniform4fv(u_mode, 1, skin_mode);
        // POCKET_GPU_SKIN_V13M: all 7 attributes stay enabled with pointers into the current vertex buffer.
        // vitaGL decides "packed" layout and offsets from the first attributes even when they are disabled, so a
        // disabled attribute with a stale pointer can make it read the wrong memory.
        for (GLuint i = 0; i < 7; ++i) glEnableVertexAttribArray(i);
        bound_program = 1; skin_bound = true;
    }
    void use_plain_program() {
        if (bound_program == 2) return;
        if (bound_program == 0) glUseProgram(skin_program);
        static const GLfloat plain_mode[4] = {1, 0, 0, 0};
        glUniform4fv(u_mode, 1, plain_mode);
        for (GLuint i = 0; i < 7; ++i) glEnableVertexAttribArray(i); // POCKET_GPU_SKIN_V13M
        skin_xform_gen = ~0ull; // uMvp is shared: the next skinned draw uploads its own again
        bound_program = 2; skin_bound = false;
    }
    // One GVertex array in plain mode with the current GL state and the batch's fragment parameters.
    void plain_draw(GLenum mode, const GVertex *v, GLsizei n, const GLfloat matrix[16], bool variant = true) {
        use_plain_program();
        static const GLfloat identity[16] = {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
        glUniform4fv(u_mvp, 4, matrix ? matrix : identity);
        GLfloat tex[4] = {frag_tex[0], frag_tex[1], frag_tex[2], frag_tex[3]};
        if (variant && skin_debug) { // POCKET_GPU_SKIN_V13J
            if (skin_debug_last == 1) tex[3] = 1;
            if (skin_debug_last == 2) tex[0] = 0;
            if (skin_debug_last == 3 && tex[0] > 0.5f) { tex[1] = 3; tex[2] = 1; tex[3] = 1; }
        }
        glUniform4fv(u_tex, 1, tex); glUniform4fv(u_env, 1, frag_env); glUniform4fv(u_alpha, 1, frag_alpha);
        constexpr GLsizei stride = sizeof(GVertex);
        // POCKET_GPU_SKIN_V13M: the skin attributes (unused in plain mode) read harmless bytes of the same vertices;
        // aW0 is at the lowest address, as vitaGL's packed layout expects of the first attribute.
        const auto *vb = reinterpret_cast<const std::uint8_t *>(v);
        glVertexAttribPointer(0, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, vb + 0);
        glVertexAttribPointer(1, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, vb + 4);
        glVertexAttribPointer(3, 3, GL_BYTE, GL_FALSE, stride, vb + 8);
        glVertexAttribPointer(4, 3, GL_SHORT, GL_FALSE, stride, vb + 12);
        glVertexAttribPointer(6, 4, GL_FLOAT, GL_FALSE, stride, v[0].position);
        glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, stride, v[0].color);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride, v[0].uv);
        glDrawArrays(mode, 0, n);
    }
    bool plain_setup() {
        if (FILE *f = std::fopen("ux0:data/pocketprobe/no-plain-shader.flag", "rb")) {
            std::fclose(f); plain_enabled = false;
            std::fprintf(stderr, "[gpu-plain] off (no-plain-shader.flag): fixed-function batches as in v13h\n"); std::fflush(stderr);
            return true;
        }
        if (u_mode < 0) { std::fprintf(stderr, "[gpu-plain] uMode not found\n"); std::fflush(stderr); return false; }
        // Untextured draws still sample unit 0: give it a real texture.
        const std::uint32_t white = 0xFFFFFFFFu;
        glGenTextures(1, &white_texture); glBindTexture(GL_TEXTURE_2D, white_texture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, &white);
        plain_program = skin_program;
        state_valid = false; prepared = false; // rebind the real texture state at the next draw
        // POCKET_GPU_BATCH_V14: the shader clamps depth, so PMO groups and rigid draws whose depth leaves 0..65535
        // (most of the quest terrain) can stay on the GPU. Rebuild the transform so the cache sees the change.
        if (FILE *sf = std::fopen("ux0:data/pocketprobe/no-pmo-split.flag", "rb")) { std::fclose(sf); pmo_split = false; }
        if (FILE *bf = std::fopen("ux0:data/pocketprobe/no-skin-batch.flag", "rb")) { std::fclose(bf); skin_batching = false; }
        std::fprintf(stderr, "[skin-batch] v16 %s\n", skin_batching ? "on: up to 8 skinned draws per GL draw" : "off (no-skin-batch.flag)");
        std::fprintf(stderr, "[pmo-split] v15 %s\n", pmo_split ? "on: PMO groups crossing the near plane are split per triangle" : "off (no-pmo-split.flag)");
        if (FILE *nf = std::fopen("ux0:data/pocketprobe/no-depth-free.flag", "rb")) std::fclose(nf);
        else { rigid_transform.depth_check = false; rigid_transform_valid = false; }
        std::fprintf(stderr, "[gpu-batch] v14 depth range check %s\n", rigid_transform.depth_check ? "kept (no-depth-free.flag)" : "off: shader clamps depth like the PSP");
        std::fprintf(stderr, "[gpu-plain] v16 ready: one custom program for every draw (no program switches)\n");
        std::fflush(stderr);
        return true;
    }
    static GLuint skin_shader(GLenum type, const char *source, const char *what) {
        const GLuint s = glCreateShader(type);
        glShaderSource(s, 1, &source, nullptr);
        glCompileShader(s);
        GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024] = {}; GLsizei n = 0; glGetShaderInfoLog(s, sizeof log - 1, &n, log);
            std::fprintf(stderr, "[gpu-skin] %s shader compile FAILED: %s\n", what, log);
            std::fflush(stderr);
            glDeleteShader(s); return 0;
        }
        return s;
    }
    bool skin_setup() {
        if (skin_tried) return skin_ready;
        skin_tried = true;
        // POCKET_GPU_SKIN_V13B: vitaGL treats GL_VERTEX_SHADER/GL_FRAGMENT_SHADER sources as GLSL (compiled only at
        // link time, with GL_COMPILE_STATUS reporting success meanwhile); Cg needs the _EXT types. With them the
        // compile happens here and a failure is reported before glLinkProgram can touch a missing program.
        const GLuint vs = skin_shader(GL_CG_VERTEX_SHADER_EXT, pocket::skin::kVertexShader, "vertex");
        GLuint fs = skin_shader(GL_CG_FRAGMENT_SHADER_EXT, pocket::skin::kFragmentShader, "fragment");
        if (!fs && vs) { // POCKET_GPU_SKIN_V13C
            fs = skin_shader(GL_CG_FRAGMENT_SHADER_EXT, pocket::skin::kFragmentShaderSimple, "fragment (simple)");
            if (fs) { std::fprintf(stderr, "[gpu-skin] using the SIMPLE fragment shader: texture x colour only, rare texture modes may look different\n"); std::fflush(stderr); }
        }
        if (!vs || !fs) { std::fprintf(stderr, "[gpu-skin] disabled: CPU path keeps drawing these models\n"); std::fflush(stderr); return false; }
        skin_program = glCreateProgram();
        glAttachShader(skin_program, vs); glAttachShader(skin_program, fs);
        glBindAttribLocation(skin_program, 0, "aW0"); glBindAttribLocation(skin_program, 1, "aW1");
        glBindAttribLocation(skin_program, 2, "aUV"); glBindAttribLocation(skin_program, 3, "aN");
        glBindAttribLocation(skin_program, 4, "aP");
        glBindAttribLocation(skin_program, 5, "aColor"); glBindAttribLocation(skin_program, 6, "aPos"); // V13L
        glLinkProgram(skin_program);
        GLint ok = 0; glGetProgramiv(skin_program, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024] = {}; GLsizei n = 0; glGetProgramInfoLog(skin_program, sizeof log - 1, &n, log);
            std::fprintf(stderr, "[gpu-skin] link FAILED: %s; disabled\n", log); std::fflush(stderr); return false;
        }
        auto loc = [&](const char *name) {
            GLint l = glGetUniformLocation(skin_program, name);
            if (l < 0) { char indexed[64]; std::snprintf(indexed, sizeof indexed, "%s[0]", name); l = glGetUniformLocation(skin_program, indexed); }
            if (l < 0) std::fprintf(stderr, "[gpu-skin] uniform %s not found\n", name);
            return l;
        };
        u_bones = loc("uBones"); u_world = loc("uWorld"); u_mvp = loc("uMvp"); u_uvs = loc("uUvs"); u_uvt = loc("uUvt");
        u_lpos = loc("uLpos"); u_ldir = loc("uLdir"); u_latt = loc("uLatt"); u_lspot = loc("uLspot");
        u_lamb = loc("uLamb"); u_ldif = loc("uLdif"); u_lspe = loc("uLspe");
        u_mamb = loc("uMamb"); u_mdif = loc("uMdif"); u_mspe = loc("uMspe"); u_base = loc("uBase"); u_params = loc("uParams"); u_near = loc("uNear"); u_mode = loc("uMode");
        u_tex = loc("uTex"); u_env = loc("uEnv"); u_alpha = loc("uAlpha"); u_sampler = loc("uTexture");
        if (u_bones < 0 || u_mvp < 0 || u_params < 0 || u_near < 0 || u_uvs < 0) { std::fprintf(stderr, "[gpu-skin] missing uniforms; disabled\n"); std::fflush(stderr); return false; }
        glUseProgram(skin_program);
        if (u_sampler >= 0) glUniform1i(u_sampler, 0);
        glUseProgram(0); bound_program = 0;
        // POCKET_GPU_SKIN_V13I: without the plain program the skin path is not used at all (it breaks the other draws).
        if (!plain_setup()) { std::fprintf(stderr, "[gpu-plain] FAILED: GPU skin disabled, everything as without gpu-skin.flag\n"); std::fflush(stderr); return false; }
        std::fprintf(stderr, "[gpu-skin] v16 program ready (Cg vertex + fragment compiled and linked)\n"); std::fflush(stderr);
        skin_ready = true;
        return true;
    }
public:
    bool draw_skinned(const Ge &ge, unsigned type, unsigned count, unsigned vaddr, unsigned iaddr) override {
        if (!skin_enabled) return false;
        const int debug = skin_ready ? skin_debug_mode() : -1;
        if (debug == 4) return false; // POCKET_GPU_SKIN_V13J: variant 4 = skin off
        if (type < 3 || type > 5 || count < 3 || count > 65535) { ++skin_profile.prim; return false; }
        pocket::skin::Format f;
        if (!pocket::skin::format_for(ge.reg(0x12), f)) { ++skin_profile.format; return false; }
        if (ge.reg(0xc0) & 3u) { ++skin_profile.texgen; return false; }
        if (ge.reg(0xd3) & 1u) { ++skin_profile.clear; return false; }
        if (!skin_setup()) { skin_enabled = false; return false; }
        // Same transform cache as the rigid paths: model -> GL clip including the PSP viewport and screen offset.
        const auto generation = ge.rigid_transform_generation();
        if (!rigid_transform_valid || generation != rigid_generation) {
            pocket::rigid::Parameters parameters;
            parameters.world = ge.world_matrix().m; parameters.view = ge.view_matrix().m; parameters.projection = ge.proj_matrix().m;
            parameters.viewport = {gpu::float24(ge.reg(0x42)), gpu::float24(ge.reg(0x43)), gpu::float24(ge.reg(0x44)),
                gpu::float24(ge.reg(0x45)), gpu::float24(ge.reg(0x46)), gpu::float24(ge.reg(0x47)),
                float(ge.reg(0x4c) & 65535) / 16, float(ge.reg(0x4d) & 65535) / 16};
            rigid_transform_valid = rigid_transform.build(parameters); rigid_generation = generation;
            if (rigid_transform_valid) visibility_cache.set_transform(rigid_transform);
        }
        if (!rigid_transform_valid) { ++skin_profile.transform; return false; }
        // Indices and the vertex range they use, read before any GL state changes.
        const std::uint8_t *ib = f.index ? ge.guest_bytes(iaddr, count * f.index) : nullptr;
        if (f.index && !ib) { ++skin_profile.memory; return false; }
        unsigned lo = 0, hi = count - 1;
        if (f.index) {
            lo = 0xFFFFu; hi = 0;
            for (unsigned i = 0; i < count; ++i) {
                unsigned v;
                if (f.index == 1) v = ib[i]; else { std::uint16_t s; std::memcpy(&s, ib + i * 2u, 2); v = s; }
                lo = std::min(lo, v); hi = std::max(hi, v);
            }
        }
        const unsigned span = hi - lo + 1u;
        if (span > 65535u) { ++skin_profile.range; return false; }
        const std::uint8_t *src = ge.guest_bytes(vaddr + lo * f.stride, span * f.stride);
        if (!src) { ++skin_profile.memory; return false; }
        // POCKET_GPU_SKIN_V13F: only draws that need no PSP clipping go to the GPU, skinned or not (see skin::route()).
        // v13d did this for models without weights only; the village ground and walls are skinned models too, and
        // the GPU cut them at the wrong place next to the camera.
        // POCKET_GPU_SKIN_V13G: the village log showed the check never finds skinned draws that need clipping, and its
        // CPU skinning halved the frame rate: skinned models go straight to the GPU again (as v13d), the cheap check
        // stays for models without weights.
        if (!f.weights && !skin_rigid) { ++skin_profile.rigid_cpu; return false; }
        if (f.weights) ++skin_profile.rigid;
        else switch (pocket::skin::route(rigid_transform, ge, f, src, span)) {
        case pocket::skin::Route::Cpu: ++skin_profile.rigid_cpu; return false;
        case pocket::skin::Route::Culled: ++skin_profile.rigid_culled; return true;
        case pocket::skin::Route::Gpu: ++skin_profile.rigid; break;
        }
        // POCKET_GPU_BATCH_V16: consecutive skinned draws with the same GL state, lights and camera are collected and
        // drawn with ONE glDrawElements: each draw gets a slot (its bones and UV scale), its strips/fans become
        // triangles in the shared index list. Anything else drawn in between flushes the batch first (order kept).
        if (!batch.empty()) draw_batch();
        prepared = false; ++serial; prepare(ge); // a state change flushes the pending skin batch (prepare -> draw_batch)
        pocket::skin::fill_uv(ge, target_texture, float(texture_width), float(texture_height), float(Side), skin_u);
        const bool textured = (ge.reg(0x1e) & 1u) != 0;
        const unsigned func = ge.reg(0xc9);
        const GLfloat tex[4] = {textured ? 1.0f : 0.0f, float(std::min(4u, func & 7u)), (func & 0x100u) ? 1.0f : 0.0f,
                                (func & 0x10000u) ? 2.0f : 1.0f};
        const unsigned env = ge.reg(0xca);
        const GLfloat envc[4] = {float(env & 255) / 255, float((env >> 8) & 255) / 255, float((env >> 16) & 255) / 255, 1};
        const unsigned at = ge.reg(0xdb);
        const GLfloat alpha[4] = {(ge.reg(0x22) & 1u) ? 1.0f : 0.0f, float(at & 7u), float((at >> 8) & 255u) / 255, 0};
        const unsigned tri_indices = type == 3 ? count : (count - 2) * 3;
        if (sb_slots && (sb_slots >= kSkinSlots || !skin_batching || generation != skin_xform_gen ||
                         ge.light_generation() != skin_light_gen || sb_vertices.size() + span > 65535u ||
                         std::memcmp(sb_uvt, skin_u.uvt, sizeof sb_uvt) != 0 || std::memcmp(sb_tex, tex, sizeof tex) != 0 ||
                         std::memcmp(sb_env, envc, sizeof envc) != 0 || std::memcmp(sb_alpha, alpha, sizeof alpha) != 0))
            flush_skin();
        use_skin_program();
        if (generation != skin_xform_gen) {
            pocket::skin::fill_transform(ge, rigid_transform, skin_u);
            glUniform4fv(u_world, 3, &skin_u.world[0][0]); glUniform4fv(u_mvp, 4, &skin_u.mvp[0][0]);
            glUniform4fv(u_near, 1, skin_u.near); // POCKET_GPU_SKIN_V13D
            skin_xform_gen = generation;
        }
        if (ge.light_generation() != skin_light_gen) {
            pocket::skin::fill_light(ge, skin_u);
            glUniform4fv(u_lpos, 4, &skin_u.lpos[0][0]); glUniform4fv(u_ldir, 4, &skin_u.ldir[0][0]);
            glUniform4fv(u_latt, 4, &skin_u.latt[0][0]); glUniform4fv(u_lspot, 4, &skin_u.lspot[0][0]);
            glUniform4fv(u_lamb, 4, &skin_u.lamb[0][0]); glUniform4fv(u_ldif, 4, &skin_u.ldif[0][0]);
            glUniform4fv(u_lspe, 4, &skin_u.lspe[0][0]);
            glUniform4fv(u_mamb, 1, skin_u.mamb); glUniform4fv(u_mdif, 1, skin_u.mdif); glUniform4fv(u_mspe, 1, skin_u.mspe);
            glUniform4fv(u_base, 1, skin_u.base); glUniform4fv(u_params, 1, skin_u.params);
            skin_light_gen = ge.light_generation();
        }
        if (sb_slots == 0) {
            std::memcpy(sb_uvt, skin_u.uvt, sizeof sb_uvt); std::memcpy(sb_tex, tex, sizeof tex);
            std::memcpy(sb_env, envc, sizeof envc); std::memcpy(sb_alpha, alpha, sizeof alpha);
        }
        const unsigned slot = sb_slots++;
        pocket::skin::fill_bones(ge, f.weights, skin_u);
        std::memcpy(&sb_bones[slot * 24][0], &skin_u.bones[0][0], sizeof skin_u.bones);
        std::memcpy(sb_uvs[slot], skin_u.uv, sizeof skin_u.uv);
        const unsigned first = static_cast<unsigned>(sb_vertices.size());
        sb_vertices.resize(first + span);
        for (unsigned i = 0; i < span; ++i) {
            pocket::skin::repack(src + i * f.stride, f, sb_vertices[first + i]);
            sb_vertices[first + i].n[3] = static_cast<std::int8_t>(slot);
        }
        const auto at_index = [&](unsigned i) -> std::uint16_t {
            unsigned v = i;
            if (f.index == 1) v = ib[i] - lo;
            else if (f.index == 2) { std::uint16_t s; std::memcpy(&s, ib + i * 2u, 2); v = s - lo; }
            return static_cast<std::uint16_t>(first + v);
        };
        sb_indices.reserve(sb_indices.size() + tri_indices);
        if (type == 3) for (unsigned i = 0; i + 2 < count; i += 3) { sb_indices.push_back(at_index(i)); sb_indices.push_back(at_index(i + 1)); sb_indices.push_back(at_index(i + 2)); }
        else if (type == 4) for (unsigned i = 0; i + 2 < count; ++i) { // GL strip winding: odd triangles swap the first two
            if (i & 1u) { sb_indices.push_back(at_index(i + 1)); sb_indices.push_back(at_index(i)); }
            else { sb_indices.push_back(at_index(i)); sb_indices.push_back(at_index(i + 1)); }
            sb_indices.push_back(at_index(i + 2));
        }
        else for (unsigned i = 0; i + 2 < count; ++i) { sb_indices.push_back(at_index(0)); sb_indices.push_back(at_index(i + 1)); sb_indices.push_back(at_index(i + 2)); }
        if (skin_check && ((skin_profile.draws + frames) & 31u) == 0) skin_self_check(ge, f, vaddr, lo, span, type, count, &sb_vertices[first]);
        if (!skin_batching) flush_skin();
        ++skin_profile.draws; skin_profile.vertices += span;
        return true;
    }
private:
    // POCKET_GPU_SKIN_V13E: every 32nd GPU-skinned draw, up to 8 of its vertices go through the exact CPU transform
    // and through run_vertex() with the parameters the shader has right now (cached uniforms included). Any
    // difference is counted and the first ones are logged with the draw's state, to find what the village does
    // differently from the characters.
    void skin_self_check(const Ge &ge, const pocket::skin::Format &f, unsigned vaddr, unsigned lo, unsigned span,
                         unsigned type, unsigned count, const pocket::skin::Vertex *verts) {
        ++skin_check_count.draws;
        const unsigned n = std::min(span, 8u), step = std::max(1u, span / n);
        const float *vp = rigid_transform.viewport.data();
        for (unsigned k = 0; k < n; ++k) {
            const unsigned i = std::min(span - 1, k * step);
            const Vertex c = ge.reference_vertex(vaddr + (lo + i) * f.stride);
            const auto e = pocket::skin::run_vertex(skin_u, verts[i]);
            if (!std::isfinite(c.x) || !std::isfinite(c.y) || !std::isfinite(c.z) || !std::isfinite(c.w)) continue;
            ++skin_check_count.vertices;
            const float cc[4] = {c.x, c.y, c.z, c.w};
            float dpos = 0, dgl = 0, dcol = 0, duv = 0;
            for (int j = 0; j < 4; ++j)
                dpos = std::max(dpos, std::fabs(cc[j] - e.clip[j]) / std::max({1.0f, std::fabs(cc[j]), std::fabs(e.clip[j])}));
            if (c.w > 1e-6f) {
                const float w = c.w, sx = vp[0] * c.x / w + vp[3] - vp[6], sy = vp[1] * c.y / w + vp[4] - vp[7];
                const float zs = vp[2] * c.z / w + vp[5];
                const float ref[4] = {(sx * (2.0f / Side) - 1) * w, (1 - sy * (2.0f / Side)) * w,
                                      (std::clamp(zs, 0.0f, 65535.0f) * (2.0f / 65535) - 1) * w, w};
                if (c.z + c.w >= 0)
                    for (int j = 0; j < 4; ++j)
                        dgl = std::max(dgl, std::fabs(ref[j] - e.gl[j]) / std::max({1.0f, std::fabs(ref[j]), std::fabs(w)}));
            }
            const float col[4] = {std::clamp(c.r + c.sr, 0.0f, 255.0f) / 255, std::clamp(c.g + c.sg, 0.0f, 255.0f) / 255,
                                  std::clamp(c.b + c.sb, 0.0f, 255.0f) / 255, std::clamp(c.a, 0.0f, 255.0f) / 255};
            for (int j = 0; j < 4; ++j) dcol = std::max(dcol, std::fabs(col[j] - e.color[j]));
            if (!target_texture)
                duv = std::max(std::fabs(c.u - e.uv[0]), std::fabs(c.v - e.uv[1])) / std::max({1.0f, std::fabs(c.u), std::fabs(c.v)});
            const bool bp = dpos > 1e-3f, bg = dgl > 1e-3f, bc = dcol > 3.0f / 255, bu = duv > 1e-3f;
            skin_check_count.bad_pos += bp; skin_check_count.bad_gl += bg; skin_check_count.bad_color += bc; skin_check_count.bad_uv += bu;
            if ((bp || bg || bc || bu) && skin_check_logged < 40) {
                ++skin_check_logged;
                std::fprintf(stderr, "[skin-check] vt=%06X w=%u n=%d prim=%u count=%u v=%u/%u lit=%u tex=%u | pos %.3g gl %.3g col %.3g uv %.3g\n",
                    unsigned(f.vt), f.weights, int(f.normal), type, count, i, span, unsigned(ge.reg(0x17) & 1u),
                    unsigned(ge.reg(0x1e) & 1u), dpos, dgl, dcol, duv);
                std::fprintf(stderr, "[skin-check]   cpu clip %g %g %g %g rgba %.0f %.0f %.0f %.0f uv %g %g | gpu clip %g %g %g %g gl %g %g %g %g rgba %.0f %.0f %.0f %.0f uv %g %g\n",
                    c.x, c.y, c.z, c.w, col[0] * 255, col[1] * 255, col[2] * 255, col[3] * 255, c.u, c.v,
                    e.clip[0], e.clip[1], e.clip[2], e.clip[3], e.gl[0], e.gl[1], e.gl[2], e.gl[3],
                    e.color[0] * 255, e.color[1] * 255, e.color[2] * 255, e.color[3] * 255, e.uv[0], e.uv[1]);
                std::fflush(stderr);
            }
        }
    }
    void plain_state() {
        if (!(plain_active() && plain_enabled)) ffp_program(); // POCKET_GPU_SKIN_V13L
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
        pocket::phase::GeScope upload_phase(pocket::phase::GeTexUpload);
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
        pocket::phase::GeScope prepare_phase(pocket::phase::GePrepare);
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
        frag_tex[0] = 0; frag_tex[1] = 0; frag_tex[2] = 0; frag_tex[3] = 1; frag_alpha[0] = 0; // POCKET_GPU_SKIN_V13I
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
            glDisable(GL_TEXTURE_2D); if (white_texture) glBindTexture(GL_TEXTURE_2D, white_texture);
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
                frag_alpha[0] = 1; frag_alpha[1] = float(at & 7u); frag_alpha[2] = float((at >> 8) & 255u) / 255; // V13I
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
                { const unsigned func = ge.reg(0xc9); // POCKET_GPU_SKIN_V13I: same parameters as the skin path
                  frag_tex[0] = 1; frag_tex[1] = float(std::min(4u, func & 7u)); frag_tex[2] = (func & 0x100u) ? 1.0f : 0.0f;
                  frag_tex[3] = (func & 0x10000u) ? 2.0f : 1.0f;
                  frag_env[0] = color[0]; frag_env[1] = color[1]; frag_env[2] = color[2]; frag_env[3] = 1; }

            } else { glDisable(GL_TEXTURE_2D); if (white_texture) glBindTexture(GL_TEXTURE_2D, white_texture); }
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
        out.color[0] = std::clamp(color.r + (clearing ? 0 : color.sr), 0.0f, 255.0f) * (1.0f / 255.0f);
        out.color[1] = std::clamp(color.g + (clearing ? 0 : color.sg), 0.0f, 255.0f) * (1.0f / 255.0f);
        out.color[2] = std::clamp(color.b + (clearing ? 0 : color.sb), 0.0f, 255.0f) * (1.0f / 255.0f);
        out.color[3] = std::clamp(color.a, 0.0f, 255.0f) * (1.0f / 255.0f);
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
        flush_skin(); // POCKET_GPU_BATCH_V16: pending skinned draws come first (they were issued first)
        if (batch.empty()) return;
        if (plain_active() && plain_enabled) { // POCKET_GPU_SKIN_V13I
            pocket::phase::GeScope submit_phase(pocket::phase::GeSubmit);
            ++profile.draws; profile.triangles += static_cast<unsigned>(batch.size() / 3);
            if (batch_model) ++rigid_profile.batches;
            plain_draw(GL_TRIANGLES, batch.data(), static_cast<GLsizei>(batch.size()), batch_model ? batch_matrix.data() : nullptr);
            batch.clear();
            return;
        }
        ffp_program(); // POCKET_GPU_SKIN_V13: fixed-function draw follows
        pocket::phase::GeScope submit_phase(pocket::phase::GeSubmit);
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
        if (plain_active() && plain_enabled) { // POCKET_GPU_SKIN_V13I: replace, colour alpha 1
            glBindTexture(GL_TEXTURE_2D, image);
            const GVertex q[4] = {{{-1, 1, 0, 1}, {1, 1, 1, 1}, {0, top}}, {{-1, -1, 0, 1}, {1, 1, 1, 1}, {0, bottom}},
                                  {{1, 1, 0, 1}, {1, 1, 1, 1}, {u, top}}, {{1, -1, 0, 1}, {1, 1, 1, 1}, {u, bottom}}};
            frag_tex[0] = 1; frag_tex[1] = 3; frag_tex[2] = 0; frag_tex[3] = 1; frag_alpha[0] = 0;
            plain_draw(GL_TRIANGLE_STRIP, q, 4, nullptr, false);
            return;
        }
        ffp_program(); // POCKET_GPU_SKIN_V13
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
        std::fprintf(stderr, "[gpu-state] v7 enabled; GL state key ignores bone count/vertex format (except 2D bit) and UV scale\n");
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
        if (FILE *f = std::fopen("ux0:data/pocketprobe/gpu-skin.flag", "rb")) { std::fclose(f); skin_enabled = true; }
        std::fprintf(stderr, "[gpu-skin] v13 %s; skinning + PSP lighting in a vertex shader for u8-weight/s16 models\n",
                     skin_enabled ? "enabled by gpu-skin.flag" : "off (create gpu-skin.flag to try it)");
        if (FILE *f = std::fopen("ux0:data/pocketprobe/skin-weighted-only.flag", "rb")) { std::fclose(f); skin_rigid = false; }
        if (FILE *f = std::fopen("ux0:data/pocketprobe/no-skin-check.flag", "rb")) { std::fclose(f); skin_check = false; }
        if (FILE *f = std::fopen("ux0:data/pocketprobe/skin-debug.flag", "rb")) { std::fclose(f); skin_debug = true; }
        std::fprintf(stderr, "[gpu-skin] v13j debug cycle %s\n", skin_debug ? "ON (skin-debug.flag): 5 plain-shader variants x 10 s, white squares bottom-right" : "off");
        std::fprintf(stderr, "[gpu-skin] v13e self-check %s (1 draw in 32 compared with the CPU transform)\n", skin_check ? "on" : "off");
        std::fprintf(stderr, "[gpu-skin] v13f GPU only for draws needing no PSP clipping; models without weights: %s\n",
                     skin_rigid ? "GPU path" : "CPU (skin-weighted-only.flag)");
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
        if(!box_safe && pmo_split && plain_active() && plain_enabled) return draw_split_group(ge,p,us,vs,uo,vo);
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
    // POCKET_PMO_SPLIT_V15: a PMO group that crosses the near plane or the guard band. Before, one such vertex
    // sent the WHOLE group (the quest ground around the player, ~100 groups a frame) to the CPU path. Now each
    // triangle is decided alone: inside -> GPU as before; certainly rejected by the PSP -> dropped; otherwise
    // clipped on the CPU exactly like Ge::draw_triangle (pocket::clip, tested against Ge) and drawn as screen
    // vertices. Without blending the clipped triangles are drawn after the group's GPU triangles; with blending
    // the original order is kept.
    bool draw_split_group(const Ge &ge,const pocket::pmo::Geometry &p,float us,float vs,float uo,float vo) {
        using V=pocket::rigid::Transform::Visibility;
        const std::size_t nv=p.vertices.size();
        pmo_vis.resize(nv);
        bool any_safe=false,any_boundary=false;
        for(std::size_t i=0;i<nv;++i) {
            const auto &v=p.vertices[i];
            const std::int16_t xyz[]={static_cast<std::int16_t>(v.position[0]*32768.0f),
                static_cast<std::int16_t>(v.position[1]*32768.0f),static_cast<std::int16_t>(v.position[2]*32768.0f)};
            const auto c=visibility_cache.classify(reinterpret_cast<const std::uint8_t*>(xyz));
            pmo_vis[i]=static_cast<std::uint8_t>(c);
            any_safe|=c==V::Safe; any_boundary|=c==V::Boundary;
        }
        if(!any_safe&&!any_boundary) { if(!rigid_cull)return false; ++pmo_gpu_culled; return true; }
        prepared=false;++serial;prepare(ge);
        const bool keep_order=(ge.reg(0x21)&1u)!=0;
        const auto &M=rigid_transform.clip;
        auto uv_fix=[&](float &u,float &v){ if(target_texture){u*=float(texture_width)/Side;v=1-v*(float(texture_height)/Side);} };
        auto clip_vertex=[&](const pocket::pmo::Input &v){
            const float x=v.position[0],y=v.position[1],z=v.position[2];
            return pocket::clip::ClipVertex{M[0]*x+M[4]*y+M[8]*z+M[12],M[1]*x+M[5]*y+M[9]*z+M[13],
                M[2]*x+M[6]*y+M[10]*z+M[14],M[3]*x+M[7]*y+M[11]*z+M[15],
                v.uv[0]*us+uo,v.uv[1]*vs+vo,v.color[0],v.color[1],v.color[2],v.color[3]};
        };
        pmo_clipped.clear();
        unsigned gpu_tris=0,cpu_tris=0;
        for(unsigned i=0;i+2<p.triangles.size();i+=3) {
            const unsigned ia=p.triangles[i],ib=p.triangles[i+1],ic=p.triangles[i+2];
            const V a=V(pmo_vis[ia]),b=V(pmo_vis[ib]),c=V(pmo_vis[ic]);
            if(a==V::Safe&&b==V::Safe&&c==V::Safe) {
                begin_batch(true,&rigid_transform.gpu);
                const auto &last=p.vertices[ic];
                for(unsigned k:{ia,ib,ic}) {
                    const auto &v=p.vertices[k];GVertex out;
                    std::copy_n(v.position,4,out.position);std::copy_n(flat?last.color:v.color,4,out.color);
                    out.uv[0]=v.uv[0]*us+uo;out.uv[1]=v.uv[1]*vs+vo;uv_fix(out.uv[0],out.uv[1]);
                    batch.push_back(out);
                }
                ++gpu_tris;
                if(batch.size()>=12288)draw_batch();
                continue;
            }
            if(a==V::OutsideGuard||b==V::OutsideGuard||c==V::OutsideGuard)continue; // the PSP drops it
            if(a==V::OutsideNear&&b==V::OutsideNear&&c==V::OutsideNear)continue;    // entirely behind the near plane
            const pocket::clip::ClipVertex cv[3]={clip_vertex(p.vertices[ia]),clip_vertex(p.vertices[ib]),clip_vertex(p.vertices[ic])};
            ++cpu_tris;
            auto emit=[&](const pocket::clip::GlVertex &g){
                GVertex out; std::copy_n(g.position,4,out.position); std::copy_n(g.color,4,out.color);
                out.uv[0]=g.uv[0];out.uv[1]=g.uv[1];uv_fix(out.uv[0],out.uv[1]);
                if(keep_order){ begin_batch(false); batch.push_back(out); if(batch.size()>=12288)draw_batch(); }
                else pmo_clipped.push_back(out);
            };
            pocket::clip::triangle(rigid_transform.viewport,cv,flat,float(Side),1.0f,emit);
        }
        if(!pmo_clipped.empty()) {
            begin_batch(false);
            for(const auto &v:pmo_clipped){ batch.push_back(v); if(batch.size()>=12288)draw_batch(); }
        }
        ++pmo_split_groups; pmo_split_gpu+=gpu_tris; pmo_split_cpu+=cpu_tris;
        return true;
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
        pocket::phase::GeScope tri_phase(pocket::phase::GeBackendTri);
        if (!prepared) prepare(ge);
        if (batch_model) begin_batch(false);
        batch.push_back(vertex(ge, a, flat ? c : a));
        batch.push_back(vertex(ge, b, flat ? c : b)); batch.push_back(vertex(ge, c, c));
        if (batch.size() >= 12288) draw_batch();
    }
    // POCKET_FAST_TRIS_V12: identical to triangle() per triangle, but each shared vertex is converted once.
    bool wants_unclipped() const override { return false; } // triangle_unclipped() is not used by this backend
    void triangles(const Ge &ge, const Vertex *v, unsigned count, const std::uint16_t *index, unsigned index_count) override {
        pocket::phase::GeScope tri_phase(pocket::phase::GeBackendTri);
        if (!prepared) prepare(ge);
        if (flat) { // the colour of each triangle comes from its last vertex: keep the per-triangle path
            for (unsigned i = 0; i + 2 < index_count; i += 3) triangle(ge, v[index[i]], v[index[i + 1]], v[index[i + 2]]);
            return;
        }
        if (batch_model) begin_batch(false);
        if (fast_converted.size() < count) { fast_converted.resize(count); fast_stamp.resize(count, 0); }
        if (++fast_serial == 0) { std::fill(fast_stamp.begin(), fast_stamp.end(), 0u); fast_serial = 1; }
        for (unsigned i = 0; i + 2 < index_count; i += 3) {
            for (unsigned k = 0; k < 3; ++k) {
                const unsigned at = index[i + k];
                if (fast_stamp[at] != fast_serial) { fast_converted[at] = vertex(ge, v[at], v[at]); fast_stamp[at] = fast_serial; }
                batch.push_back(fast_converted[at]);
            }
            if (batch.size() >= 12288) draw_batch();
        }
    }
    std::vector<GVertex> fast_converted;
    std::vector<std::uint32_t> fast_stamp;
    std::uint32_t fast_serial = 0;
    void sprite(const Ge &ge, const Vertex &a, const Vertex &b) override {
        pocket::phase::GeScope tri_phase(pocket::phase::GeBackendTri);
        Vertex tl = b, tr = b, bl = b;
        tl.x = a.x; tl.y = a.y; tl.u = a.u; tl.v = a.v;
        tr.y = a.y; tr.v = a.v; bl.x = a.x; bl.u = a.u;
        tl.w = tr.w = bl.w = 1;
        Vertex br = b; br.w = 1;
        triangle(ge, tl, tr, bl); triangle(ge, tr, br, bl);
    }
    void flush() override { pocket::phase::GeScope flush_phase(pocket::phase::GeFlush); draw_batch(); { Cost cost(profile.flush); glFlush(); } prepared = false; state_valid = false; clear_validations(); }
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
        pocket::phase::GeScope present_phase(pocket::phase::GePresent);
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
        skin_debug_marker(); // POCKET_GPU_SKIN_V13G
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
            if (skin_enabled || skin_tried) {
                std::fprintf(stderr, "[gpu-skin] period: draws=%u gl_draws=%u vertices=%u rejected: format=%u prim=%u texgen=%u clear=%u memory=%u transform=%u range=%u | clip check: gpu=%u cpu=%u culled=%u\n",
                    skin_profile.draws, skin_gl_draws, skin_profile.vertices, skin_profile.format, skin_profile.prim, skin_profile.texgen,
                    skin_profile.clear, skin_profile.memory, skin_profile.transform, skin_profile.range,
                    skin_profile.rigid, skin_profile.rigid_cpu, skin_profile.rigid_culled);
                skin_profile = {}; skin_gl_draws = 0;
                if (skin_check) {
                    std::fprintf(stderr, "[skin-check] period: draws=%u vertices=%u bad: position=%u gl=%u colour=%u uv=%u\n",
                        skin_check_count.draws, skin_check_count.vertices, skin_check_count.bad_pos, skin_check_count.bad_gl,
                        skin_check_count.bad_color, skin_check_count.bad_uv);
                    skin_check_count = {};
                }
                { // POCKET_GPU_SKIN_V13K: the 16 most frequent fragment states of the skinned draws
                    std::vector<std::pair<std::uint64_t, CensusEntry>> rows(skin_census.begin(), skin_census.end());
                    std::sort(rows.begin(), rows.end(), [](const auto &a, const auto &b) { return a.second.draws > b.second.draws; });
                    std::fprintf(stderr, "[skin-census] period: %zu states (vt tex func rgba x2 blend mode atest ztest zfunc lit zmask fog | draws vertices fixA fixB env material)\n", rows.size());
                    for (std::size_t i = 0; i < rows.size() && i < 16; ++i) {
                        const auto k = rows[i].first; const auto &c = rows[i].second;
                        std::fprintf(stderr, "[skin-census] %06X t%u f%u a%u d%u b%u m%03X at%u zt%u zf%u l%u zm%u fg%u | %u %u %06X %06X %06X %08X\n",
                            unsigned(k & 0xFFFFFFu), unsigned((k >> 24) & 1), unsigned((k >> 25) & 7), unsigned((k >> 28) & 1),
                            unsigned((k >> 29) & 1), unsigned((k >> 30) & 1), unsigned((k >> 31) & 0xFFF), unsigned((k >> 43) & 1),
                            unsigned((k >> 44) & 1), unsigned((k >> 45) & 7), unsigned((k >> 48) & 1), unsigned((k >> 49) & 1),
                            unsigned((k >> 50) & 1), c.draws, c.vertices, unsigned(c.fix >> 24), unsigned(c.fix & 0xFFFFFFu), c.env, c.material);
                    }
                    skin_census.clear();
                }
            }
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
            std::fprintf(stderr,"[pmo-native-gpu] period: groups=%u culled=%u boundary_to_cpu=%u | split groups=%u triangles gpu=%u clipped_on_cpu=%u\n",
                pmo_gpu_groups,pmo_gpu_culled,pmo_gpu_boundary,pmo_split_groups,pmo_split_gpu,pmo_split_cpu);
            pmo_gpu_groups=pmo_gpu_culled=pmo_gpu_boundary=0; pmo_split_groups=pmo_split_gpu=pmo_split_cpu=0;
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
    bool draw_skinned(const Ge &g,unsigned type,unsigned count,std::uint32_t va,std::uint32_t ia) override { // POCKET_GPU_SKIN_V13
        bool handled=false;invoke([&]{handled=gpu->draw_skinned(g,type,count,va,ia);});return handled;
    }
    void draw_started(const gpu::DrawInfo &d) override { invoke([&]{ gpu->draw_started(d); }); }
    void triangle(const Ge &g, const Vertex &a, const Vertex &b, const Vertex &c) override {
        invoke([&]{ gpu->triangle(g,a,b,c); });
    }
    void sprite(const Ge &g, const Vertex &a, const Vertex &b) override {
        invoke([&]{ gpu->sprite(g,a,b); });
    }
    // POCKET_FAST_TRIS_V12
    bool wants_unclipped() const override { return gpu->wants_unclipped(); }
    void triangles(const Ge &g, const Vertex *v, unsigned count, const std::uint16_t *index, unsigned index_count) override {
        invoke([&]{ gpu->triangles(g, v, count, index, index_count); });
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
