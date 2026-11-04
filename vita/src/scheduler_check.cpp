// Test-only Runtime implementation: links the REAL upstream Kernel and SysMem,
// dispatches generated C++ from self-authored Allegrex instructions. Only the
// fixture delay import is implemented, not the complete HLE/runtime.
// Never link this file into a game build or with runtime.cpp.
#include "kernel.hpp"
#include "pocket_fixture.hpp"
#include <stdexcept>
namespace mhf::psp::generated { const Program &pocket_fixture_program(); }

namespace {
mhf::psp::Runtime *active_runtime{};
unsigned step{};
unsigned delay_calls{};
void dispatch(mhf::psp::Runtime &rt, mhf::psp::Context &ctx, std::uint32_t address) {
    if (address==0x08801000 && step++!=0) throw std::runtime_error("A order");
    if (address==0x08802000 && step++!=1) throw std::runtime_error("B order");
    for (const auto &f: mhf::psp::generated::pocket_fixture_program().functions)
        if (f.address==address) { f.function(rt,ctx); return; }
    throw std::runtime_error("unknown fixture address");
}
}
namespace mhf::psp {
Runtime::Runtime(RuntimeOptions options)
    : options_(std::move(options)), memory_(options_.ram_size), kernel_(std::make_unique<Kernel>(*this)) {
    if (active_runtime) throw std::runtime_error("test runtime already active");
    active_runtime = this;
}
Runtime::~Runtime() { active_runtime = nullptr; }
Runtime *Runtime::active() noexcept { return active_runtime; }
void Runtime::call(Context &ctx, std::uint32_t address) { dispatch(*this, ctx, address); }
[[noreturn]] void Runtime::fatal(const std::string &message) {
    // Returning to the scheduler avoids throwing across a fiber boundary.
    kernel_->exit_program(99);
    (void)message;
    std::abort();
}
namespace hle { void profiler_register_thread(const char *) {} }
}
namespace mhf::psp::abi {
psprecomp::GuestMemory &memory(Runtime &rt) noexcept { return rt.memory(); }
void call(Runtime &rt, Context &ctx, std::uint32_t address) { rt.call(ctx,address); }
void jump(Runtime &rt, Context &ctx, std::uint32_t address) { rt.call(ctx,address); }
void call_import(Runtime &rt, Context &ctx, ImportEntry &import) {
    if (std::string_view(import.library)!="ThreadManForUser" || import.nid!=0xCEADEB47 || ctx.gpr[4]!=2000)
        throw std::runtime_error("unexpected generated import");
    ++delay_calls;
    ctx.gpr[2]=rt.kernel().wait(Kernel::WaitType::Delay,0,rt.kernel().now_us()+ctx.gpr[4]);
    if (ctx.gpr[2]!=0 || step++!=2) throw std::runtime_error("generated delay resume");
}
std::atomic<bool> interrupt_pending{false};
void service_interrupts(Runtime &) { throw std::runtime_error("unexpected interrupt"); }
}
namespace pocket {
void check_scheduler() {
    mhf::psp::RuntimeOptions options;
    options.fiber_stack_size = 64 * 1024;
    mhf::psp::Runtime rt(options);
    auto &k = rt.kernel();
    step = 0; delay_calls = 0;
    const auto a = k.create_thread("probe-high", 0x08801000, 0x20, 4096, 0);
    const auto b = k.create_thread("probe-low", 0x08802000, 0x30, 4096, 0);
    if (a < 0 || b < 0 || k.start_thread(a, 0, 0) != 0 || k.start_thread(b, 0, 0) != 0)
        throw std::runtime_error("scheduler thread creation failed");
    if (k.run_scheduler() != 0 || step != 3 || delay_calls != 1 ||
        k.thread(a)->exit_status != 7 || k.thread(b)->exit_status != 9)
        throw std::runtime_error("scheduler ordering, delay or exit status failed");
}
}
