#include "fiber.hpp"
#include <cstdint>
#include <stdexcept>

namespace pocket {
namespace {
using mhf::psp::Fiber;
struct State {
    Fiber *host{}, *a{}, *b{};
    unsigned step{};
    bool failed{};
};
void worker_a(void *argument) {
    auto &s = *static_cast<State *>(argument);
    for (unsigned round = 0;; ++round) {
        volatile std::uint32_t local = 0xabc00000u + round;
        volatile double fp = 1.25 + round;
        if (s.step != round * 2) s.failed = true;
        ++s.step;
        Fiber::switch_to(*s.a, *s.b);
        if (local != 0xabc00000u + round || fp != 1.25 + round) s.failed = true;
    }
}
void worker_b(void *argument) {
    auto &s = *static_cast<State *>(argument);
    for (unsigned round = 0;; ++round) {
        volatile std::uint32_t local = 0xdef00000u + round;
        volatile double fp = 2.5 + round;
        if (s.step != round * 2 + 1) s.failed = true;
        ++s.step;
        Fiber::switch_to(*s.b, *s.host);
        if (local != 0xdef00000u + round || fp != 2.5 + round) s.failed = true;
    }
}
}
void check_fibers() {
    State state;
    auto host = Fiber::from_current_thread();
    Fiber a(64 * 1024, worker_a, &state);
    Fiber b(64 * 1024, worker_b, &state);
    state.host = host.get(); state.a = &a; state.b = &b;
    for (unsigned round = 0; round < 129; ++round) {
        Fiber::switch_to(*host, a);
        if (state.failed || state.step != (round + 1) * 2)
            throw std::runtime_error("fiber ordering or stack/FP preservation failed");
    }
}
}
