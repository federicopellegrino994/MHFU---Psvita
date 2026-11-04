// Link audit only: references every generated program and the real Runtime.
// No game execution, file loading or packaging occurs here.
#include "mhf/psp/generated.hpp"
#include "mhf/psp/runtime.hpp"
#include <cstdio>
namespace mhf::psp::generated {
std::span<const Program *const> mhfu_programs();
}
int main() {
    mhf::psp::Runtime runtime;
    const auto programs = mhf::psp::generated::mhfu_programs();
    if (programs.empty()) return 1;
    runtime.add_program(*programs.front());
    for (std::size_t i = 1; i < programs.size(); ++i) runtime.add_overlay(*programs[i]);
    std::printf("Linked %u programs; game execution not requested.\n", static_cast<unsigned>(programs.size()));
    return 0;
}
