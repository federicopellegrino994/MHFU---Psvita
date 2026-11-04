#include "probe.hpp"
#include "iso9660.hpp"
#include "param_sfo.hpp"
#include "psprecomp/guest_memory.hpp"
#include "psprecomp/allegrex_context.hpp"
#include <exception>
#include <stdexcept>

// This standalone diagnostic does not link either PSP runtime. GuestMemory's
// optional write-watch logger needs these three diagnostic callbacks only.
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return -1; }
const char *runtime_thread_name() noexcept { return "vita-probe"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}

namespace pocket {
void check_fibers();
void check_scheduler();
Report inspect(const std::string &iso_path) {
    Report r;
#if defined(POCKET_FULL_RUNTIME)
    r.lines.emplace_back("POCKET PROBE 0.5 - full Runtime, offline");
#else
    r.lines.emplace_back("POCKET PROBE 0.4 - native Vita bring-up");
#endif
    r.lines.emplace_back("Diagnostic only. Game code is not connected.");
    try {
        check_fibers();
        r.fibers_ok = true;
        r.lines.emplace_back("PASS: fibers, 129 cycles, host/A/B, stack and FP values");
    } catch (const std::exception &e) {
        r.lines.emplace_back(std::string("FAIL: fibers: ") + e.what());
    }
    try {
        check_scheduler();
        r.scheduler_ok = true;
#if defined(POCKET_FULL_RUNTIME)
        r.lines.emplace_back("PASS: full Runtime + real delay HLE + AOT + offline");
#else
        r.lines.emplace_back("PASS: recompiled Allegrex + delay import + PSP Kernel");
#endif
    } catch (const std::exception &e) {
        r.lines.emplace_back(std::string("FAIL: scheduler: ") + e.what());
    }
    try {
        psprecomp::GuestMemory memory;
        auto view = memory.aot_fast_view();
        view.aot_store32(0x08804000, 0x1234abcd);
        if (view.aot_load32(0x48804000) != 0x1234abcd ||
            view.aot_load32(0x88804000) != 0x1234abcd)
            throw std::runtime_error("RAM aliases differ");
        view.aot_store32(0x04000000, 0x5678ef01);
        if (view.aot_load32(0x04200000) != 0x5678ef01)
            throw std::runtime_error("VRAM mirrors differ");
        bool rejected = false;
        try { (void)view.aot_load32(0x09ffffff); }
        catch (const std::exception &) { rejected = true; }
        if (!rejected) throw std::runtime_error("RAM boundary was not rejected");
        psprecomp::AllegrexContext ctx;
        ctx.set_fpr_bits(0, 0x3f800000);
        if (ctx.fpr[0] != 1.0f || ctx.fpr_bits(0) != 0x3f800000)
            throw std::runtime_error("FPU bit conversion differs");
        r.memory_ok = true;
        r.lines.emplace_back("PASS: 32 MiB PSP RAM, 2 MiB VRAM, aliases and boundary");
        r.lines.emplace_back("PASS: Allegrex context FPU bit conversion");
    } catch (const std::exception &e) {
        r.lines.emplace_back(std::string("FAIL: core memory check: ") + e.what());
    }
    try {
        mhf::psp::IsoImage iso(iso_path);
        const auto sfo_entry = iso.find("/PSP_GAME/PARAM.SFO");
        if (!sfo_entry || sfo_entry->is_directory || sfo_entry->size > 65536)
            throw std::runtime_error("missing or oversized PARAM.SFO");
        auto sfo = mhf::psp::ParamSfo::parse(iso.read_file(*sfo_entry, 0, sfo_entry->size));
        if (!sfo) throw std::runtime_error("malformed PARAM.SFO");
        const auto id = sfo->string("DISC_ID");
        const auto version = sfo->string("DISC_VERSION");
        r.lines.emplace_back("Disc: " + id + "  Version: " + version);
        const bool known = id == "ULES01213" || id == "ULUS10391" || id == "ULJM05500";
        if (!known || version != "1.01")
            throw std::runtime_error("disc ID/version not in upstream region profiles");
        const auto exe = iso.find("/PSP_GAME/SYSDIR/EBOOT.BIN");
        const auto data = iso.find("/PSP_GAME/USRDIR/DATA.BIN");
        if (!exe || exe->is_directory || exe->size == 0 ||
            !data || data->is_directory || data->size == 0)
            throw std::runtime_error("missing executable or DATA.BIN");
        r.lines.emplace_back("EBOOT.BIN: " + std::to_string(exe->size) + " bytes");
        r.lines.emplace_back("DATA.BIN: " + std::to_string(data->size) + " bytes");
        r.disc_recognized = true;
        r.lines.emplace_back("PASS: disc structure recognized (hashes not checked)");
    } catch (const std::exception &e) {
        r.lines.emplace_back(std::string("DISC: ") + e.what());
    }
    return r;
}
}
