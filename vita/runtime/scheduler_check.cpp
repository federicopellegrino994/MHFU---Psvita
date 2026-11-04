// Exercise the real Runtime, import resolver and HLE with self-authored AOT code.
#include "kernel.hpp"
#include "pocket_fixture.hpp"
#include "adhoc_link_aemu.hpp"
#include "adhoc_sockets.hpp"
#include <stdexcept>
#include <cstdio>
namespace mhf::psp::generated { const Program &pocket_fixture_program(); }
namespace {
unsigned step;
bool order_ok;
void first(mhf::psp::Runtime &rt, mhf::psp::Context &ctx) {
    order_ok = order_ok && step++ == 0;
    mhf_gen::pocket_fixture::f_08801000(rt, ctx);
    order_ok = order_ok && step++ == 2;
}
void second(mhf::psp::Runtime &rt, mhf::psp::Context &ctx) {
    order_ok = order_ok && step++ == 1;
    mhf_gen::pocket_fixture::f_08802000(rt, ctx);
}
}
namespace pocket {
void check_scheduler() {
    using namespace mhf::psp;
    RuntimeOptions options;
    options.fiber_stack_size = 128 * 1024;
    options.font_directory = "app0:";
    std::fprintf(stderr, "[probe] constructing full Runtime\n"); std::fflush(stderr);
    Runtime rt(options);
    std::fprintf(stderr, "[probe] full Runtime constructed\n"); std::fflush(stderr);
    auto program = generated::pocket_fixture_program();
    auto functions = std::vector<FunctionEntry>(program.functions.begin(), program.functions.end());
    for (auto &f : functions) {
        if (f.address == 0x08801000) f.function = first;
        if (f.address == 0x08802000) f.function = second;
    }
    program.functions = functions;
    for (auto &i : program.imports) i.hle = nullptr;
    rt.add_program(program);
    if (!rt.find_hle("ThreadManForUser", 0xCEADEB47))
        throw std::runtime_error("real delay HLE missing");
    rt.set_adhoc_server("offline.invalid:27312");
    if (!rt.adhoc_server().empty()) throw std::runtime_error("offline server accepted");
    adhoc::Sockets sockets({});
    if (sockets.pdp_create(3000) >= 0 || sockets.ptp_listen(3000) >= 0 || !sockets.empty())
        throw std::runtime_error("offline transport reported success");
    auto &k = rt.kernel();
    step = 0; order_ok = true;
    const auto a = k.create_thread("full-high", 0x08801000, 0x20, 4096, 0);
    const auto b = k.create_thread("full-low", 0x08802000, 0x30, 4096, 0);
    if (a < 0 || b < 0 || k.start_thread(a, 0, 0) || k.start_thread(b, 0, 0))
        throw std::runtime_error("full runtime thread creation failed");
    std::fprintf(stderr, "[probe] starting real HLE scheduler test\n"); std::fflush(stderr);
    if (k.run_scheduler() != 0 || !order_ok || step != 3 ||
        k.thread(a)->exit_status != 7 || k.thread(b)->exit_status != 9 ||
        program.imports.empty() || program.imports.front().hle == nullptr)
        throw std::runtime_error("full runtime AOT/import/scheduler check failed");
    std::fprintf(stderr, "[probe] full Runtime test passed\n"); std::fflush(stderr);
}
}
