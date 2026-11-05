#pragma once
// POCKET_PHASE_PROBE_V9: where the GE worker and the game thread spend their time, without reading clocks.
//
// Each thread stores the id of the phase it is in into one global (a relaxed atomic store, a couple of instructions).
// A separate sampler thread reads both globals every ~0.5 ms and counts; every 5 s it prints the shares. Unlike timer
// based probes this costs nothing measurable in the measured threads and has no clock-read bias (a clock read costs
// ~3.5 us on the Vita). Enabled only when ux0:data/pocketprobe/phase.flag exists: otherwise no sampler runs and the
// stores go to an otherwise unused variable.

#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <utility>

namespace pocket::phase {

enum Ge : std::uint8_t {
    GeIdle = 0,        // worker waiting for work
    GeLoop,            // worker loop outside the GE (queue handling, callbacks)
    GeCommands,        // display list fetch/dispatch
    GeState,           // state commands (Ge::execute)
    GePrimSetup,       // draw_primitive: draw info, draw_started
    GeRigid,           // backend draw_rigid attempt (incl. its own decode and checks)
    GeVertices,        // CPU decode + transform (skinning, matrices)
    GeLight,           // CPU lighting (inside transform)
    GeAssembly,        // triangle assembly, near clipping, to_screen, guard band
    GeBackendTri,      // backend triangle()/sprite(): GL vertex conversion and batching
    GePrepare,         // backend prepare(): GL state (slow path)
    GeTexHash,         // texture validation hash
    GeTexUpload,       // texture decode + upload
    GeSubmit,          // backend draw_batch(): glDrawArrays
    GeFlush,           // backend flush(): glFlush at list end
    GePresent,         // backend present(): framebuffer to screen, vglSwapBuffers
    GePmo,             // native PMO group path
    GeTransfer,        // block transfers
    GeSkinCheck,       // POCKET_PHASE_V16C: skin path format/index checks
    GeSkinPack,        // skin path vertex repack + index list
    GeSkinUniform,     // skin path parameters (fill + glUniform)
    GeSkinFlush,       // skin batch flush (GL calls except the draw itself)
    GeSkinSelf,        // skin self-check
    GeCount
};
enum Game : std::uint8_t {
    GameRun = 0,       // recompiled game code, interpreter, HLE calls
    GameWaitGe,        // sceGeDrawSync / sceGeListSync waiting for the GE
    GameIdle,          // scheduler has nothing to run (vblank waits, sleeps)
    GameInterp,        // POCKET_GAME_PHASES_V10: MIPS interpreter (code the recompiler did not reach)
    GameHle,           // POCKET_GAME_PHASES_V10: emulated PSP system functions (HLE imports)
    GameCount
};

inline std::atomic<std::uint8_t> ge_phase{GeIdle};
inline std::atomic<std::uint8_t> game_phase{GameRun};
inline std::atomic<int> game_waiting_ge{0}; // fibers inside sceGeDrawSync/sceGeListSync
// POCKET_GAME_HOTSPOTS_V11: what the game thread is doing inside "run" and "hle".
inline std::atomic<const char *> game_hle_name{nullptr};        // HLE function being executed
inline std::atomic<const std::uint32_t *> game_ra{nullptr};      // $ra of the running fiber (guest call site)

// Small counting table for the sampler (at most 64 distinct keys; extra keys are counted as "other").
struct Hist {
    std::array<std::uintptr_t, 64> key{};
    std::array<std::uint32_t, 64> count{};
    unsigned used = 0;
    std::uint32_t other = 0, total = 0;
    void add(std::uintptr_t k) noexcept {
        ++total;
        for (unsigned i = 0; i < used; ++i) if (key[i] == k) { ++count[i]; return; }
        if (used < key.size()) { key[used] = k; count[used] = 1; ++used; } else ++other;
    }
    template <class Print> void top(char *line, int &n, int size, unsigned limit, Print print) const {
        std::array<unsigned, 64> order{};
        for (unsigned i = 0; i < used; ++i) order[i] = i;
        for (unsigned i = 0; i < used && i < limit; ++i) // partial selection sort
            for (unsigned j = i + 1; j < used; ++j) if (count[order[j]] > count[order[i]]) std::swap(order[i], order[j]);
        for (unsigned i = 0; i < used && i < limit && n > 0 && n < size; ++i)
            n += print(line + n, size - n, key[order[i]], total ? 100.0 * count[order[i]] / total : 0.0);
        if (other && n > 0 && n < size) n += std::snprintf(line + n, size - n, " other=%.1f%%", 100.0 * other / total);
    }
    void clear() noexcept { used = 0; other = total = 0; }
};

// RAII: enter a phase, restore the previous one on exit (so nesting attributes time to the innermost phase).
struct GeScope {
    std::uint8_t previous;
    explicit GeScope(Ge p) noexcept : previous(ge_phase.load(std::memory_order_relaxed)) {
        ge_phase.store(p, std::memory_order_relaxed);
    }
    ~GeScope() { ge_phase.store(previous, std::memory_order_relaxed); }
    GeScope(const GeScope &) = delete;
    GeScope &operator=(const GeScope &) = delete;
};
struct GameScope {
    std::uint8_t previous;
    explicit GameScope(Game p) noexcept : previous(game_phase.load(std::memory_order_relaxed)) {
        game_phase.store(p, std::memory_order_relaxed);
    }
    ~GameScope() { game_phase.store(previous, std::memory_order_relaxed); }
    GameScope(const GameScope &) = delete;
    GameScope &operator=(const GameScope &) = delete;
};

// Starts the sampler once (call from any thread; later calls do nothing).
inline void start_sampler() {
#if defined(__vita__)
    static std::atomic<bool> started{false};
    if (started.exchange(true)) return;
    FILE *flag = std::fopen("ux0:data/pocketprobe/phase.flag", "rb");
    if (!flag) return;
    std::fclose(flag);
    std::fprintf(stderr, "[phase] v11 sampler enabled by phase.flag; ~2000 samples/s, report every 5 s\n");
    std::thread([] {
        static const char *const ge_names[GeCount] = {"idle", "loop", "commands", "state", "prim_setup", "rigid",
            "vertices", "light", "assembly", "backend_tri", "prepare", "tex_hash", "tex_upload", "submit", "flush",
            "present", "pmo", "transfer", "skin_check", "skin_pack", "skin_uniform", "skin_flush", "skin_self"};
        static const char *const game_names[GameCount] = {"run", "wait_ge", "idle", "interp", "hle"};
        std::array<std::uint32_t, GeCount> ge{};
        std::array<std::uint32_t, GameCount> game{};
        std::uint32_t total = 0;
        Hist hle_hist, ra_hist;
        auto report = std::chrono::steady_clock::now() + std::chrono::seconds(5);
        for (;;) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            const unsigned g = ge_phase.load(std::memory_order_relaxed), m = game_phase.load(std::memory_order_relaxed);
            if (g < GeCount) ++ge[g];
            if (m < GameCount) ++game[m];
            if (m == GameHle) {
                if (const char *name = game_hle_name.load(std::memory_order_relaxed)) hle_hist.add(reinterpret_cast<std::uintptr_t>(name));
            } else if (m == GameRun) {
                if (const std::uint32_t *ra = game_ra.load(std::memory_order_relaxed)) ra_hist.add(*ra);
            }
            ++total;
            if ((total & 63u) != 0) continue; // read the clock only every 64 samples
            const auto now = std::chrono::steady_clock::now();
            if (now < report) continue;
            report = now + std::chrono::seconds(5);
            char line[640];
            int n = std::snprintf(line, sizeof line, "[phase] ge samples=%u", static_cast<unsigned>(total));
            for (unsigned i = 0; i < GeCount && n > 0 && n < int(sizeof line); ++i)
                if (ge[i]) n += std::snprintf(line + n, sizeof line - n, " %s=%.1f%%", ge_names[i], 100.0 * ge[i] / total);
            std::fprintf(stderr, "%s\n", line);
            n = std::snprintf(line, sizeof line, "[phase] game");
            for (unsigned i = 0; i < GameCount && n > 0 && n < int(sizeof line); ++i)
                n += std::snprintf(line + n, sizeof line - n, " %s=%.1f%%", game_names[i], 100.0 * game[i] / total);
            std::fprintf(stderr, "%s\n", line);
            char hot[1024];
            n = std::snprintf(hot, sizeof hot, "[phase-hle] samples=%u", static_cast<unsigned>(hle_hist.total));
            hle_hist.top(hot, n, int(sizeof hot), 10, [](char *o, int sz, std::uintptr_t k, double pct) {
                return std::snprintf(o, sz, " %s=%.1f%%", reinterpret_cast<const char *>(k), pct); });
            std::fprintf(stderr, "%s\n", hot);
            n = std::snprintf(hot, sizeof hot, "[phase-run-ra] samples=%u", static_cast<unsigned>(ra_hist.total));
            ra_hist.top(hot, n, int(sizeof hot), 16, [](char *o, int sz, std::uintptr_t k, double pct) {
                return std::snprintf(o, sz, " %08X=%.1f%%", static_cast<unsigned>(k), pct); });
            std::fprintf(stderr, "%s\n", hot);
            hle_hist.clear(); ra_hist.clear();
            ge = {}; game = {}; total = 0;
        }
    }).detach();
#endif
}

} // namespace pocket::phase
