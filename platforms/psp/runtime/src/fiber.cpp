#include "fiber.hpp"

#include <cstdint>
#include <cstdlib>
#include <stdexcept>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#elif defined(__vita__)
#include <psp2/fiber.h>
#include <psp2/sysmodule.h>
#include <malloc.h>
#include <limits>
#else
#include <ucontext.h>
#include <vector>
#endif

namespace mhf::psp {

#if defined(_WIN32)

struct Fiber::Impl {
    LPVOID handle{};
    bool owns{};
    Entry entry{};
    void *argument{};
};

namespace {
void WINAPI fiber_start(LPVOID parameter) {
    auto *impl = static_cast<Fiber::Impl *>(parameter);
    impl->entry(impl->argument);
    std::abort(); // a fiber entry must never return
}
} // namespace

std::unique_ptr<Fiber> Fiber::from_current_thread() {
    std::unique_ptr<Fiber> fiber{new Fiber()};
    fiber->impl_ = std::make_unique<Impl>();
    fiber->impl_->handle = IsThreadAFiber() ? GetCurrentFiber() : ConvertThreadToFiber(nullptr);
    if (fiber->impl_->handle == nullptr) {
        throw std::runtime_error("ConvertThreadToFiber failed");
    }
    return fiber;
}

Fiber::Fiber(std::size_t stack_size, Entry entry, void *argument) : impl_(std::make_unique<Impl>()) {
    impl_->entry = entry;
    impl_->argument = argument;
    impl_->owns = true;
    impl_->handle = CreateFiber(stack_size, &fiber_start, impl_.get());
    if (impl_->handle == nullptr) {
        throw std::runtime_error("CreateFiber failed");
    }
}

Fiber::~Fiber() {
    if (impl_ && impl_->owns && impl_->handle != nullptr) {
        DeleteFiber(impl_->handle);
    }
}

void Fiber::switch_to(Fiber & /*from*/, Fiber &to) { SwitchToFiber(to.impl_->handle); }

#elif defined(__vita__)

struct Fiber::Impl {
    SceFiber context{};
    void *stack{};
    Entry entry{};
    void *argument{};
    bool host_thread{};
};

namespace {
void require_fiber_module() {
    static const bool loaded = [] {
        return sceSysmoduleIsLoaded(SCE_SYSMODULE_FIBER) == 0 ||
               sceSysmoduleLoadModule(SCE_SYSMODULE_FIBER) >= 0;
    }();
    if (!loaded) throw std::runtime_error("cannot load SceFiber module");
}
void vita_fiber_start(SceUInt32 parameter, SceUInt32) {
    auto *impl = reinterpret_cast<Fiber::Impl *>(static_cast<std::uintptr_t>(parameter));
    impl->entry(impl->argument);
    std::abort(); // same contract as the desktop implementations
}
}

std::unique_ptr<Fiber> Fiber::from_current_thread() {
    require_fiber_module();
    std::unique_ptr<Fiber> fiber{new Fiber()};
    fiber->impl_ = std::make_unique<Impl>();
    fiber->impl_->host_thread = true;
    return fiber;
}

Fiber::Fiber(std::size_t stack_size, Entry entry, void *argument) : impl_(std::make_unique<Impl>()) {
    require_fiber_module();
    if (!entry || stack_size < 4096 || stack_size > std::numeric_limits<SceSize>::max() - 15u)
        throw std::runtime_error("invalid Vita fiber entry or stack size");
    const auto aligned_size = (stack_size + 15u) & ~std::size_t{15u};
    impl_->stack = memalign(16, aligned_size);
    if (!impl_->stack) throw std::bad_alloc();
    impl_->entry = entry;
    impl_->argument = argument;
    char name[] = "mhf-fiber";
    const int result = _sceFiberInitializeImpl(&impl_->context, name, vita_fiber_start,
        static_cast<SceUInt32>(reinterpret_cast<std::uintptr_t>(impl_.get())),
        impl_->stack, static_cast<SceSize>(aligned_size), nullptr);
    if (result < 0) {
        std::free(impl_->stack);
        impl_->stack = nullptr;
        throw std::runtime_error("SceFiber initialization failed: " + std::to_string(result));
    }
}

Fiber::~Fiber() {
    if (impl_ && !impl_->host_thread && impl_->stack) {
        sceFiberFinalize(&impl_->context);
        std::free(impl_->stack);
    }
}

void Fiber::switch_to(Fiber &from, Fiber &to) {
    int result;
    if (from.impl_->host_thread && to.impl_->host_thread) std::abort();
    if (from.impl_->host_thread) result = sceFiberRun(&to.impl_->context, 0, nullptr);
    else if (to.impl_->host_thread) result = sceFiberReturnToThread(0, nullptr);
    else result = sceFiberSwitch(&to.impl_->context, 0, nullptr);
    if (result < 0) std::abort();
}

#else

struct Fiber::Impl {
    ucontext_t context{};
    std::vector<unsigned char> stack;
    Entry entry{};
    void *argument{};
};

namespace {
// makecontext only passes int arguments; split the pointer in two halves.
void fiber_start(unsigned int high, unsigned int low) {
    const auto bits = (static_cast<std::uintptr_t>(high) << 32u) | static_cast<std::uintptr_t>(low);
    auto *impl = reinterpret_cast<Fiber::Impl *>(bits);
    impl->entry(impl->argument);
    std::abort(); // a fiber entry must never return
}
} // namespace

std::unique_ptr<Fiber> Fiber::from_current_thread() {
    std::unique_ptr<Fiber> fiber{new Fiber()};
    fiber->impl_ = std::make_unique<Impl>();
    return fiber; // its context is filled by the first switch away from it
}

Fiber::Fiber(std::size_t stack_size, Entry entry, void *argument) : impl_(std::make_unique<Impl>()) {
    impl_->entry = entry;
    impl_->argument = argument;
    impl_->stack.resize(stack_size);
    if (getcontext(&impl_->context) != 0) {
        throw std::runtime_error("getcontext failed");
    }
    impl_->context.uc_stack.ss_sp = impl_->stack.data();
    impl_->context.uc_stack.ss_size = impl_->stack.size();
    impl_->context.uc_link = nullptr;
    const auto bits = reinterpret_cast<std::uintptr_t>(impl_.get());
    makecontext(&impl_->context, reinterpret_cast<void (*)()>(&fiber_start), 2,
                static_cast<unsigned int>(bits >> 32u), static_cast<unsigned int>(bits & 0xFFFFFFFFu));
}

Fiber::~Fiber() = default;

void Fiber::switch_to(Fiber &from, Fiber &to) {
    if (swapcontext(&from.impl_->context, &to.impl_->context) != 0) {
        std::abort();
    }
}

#endif

} // namespace mhf::psp
