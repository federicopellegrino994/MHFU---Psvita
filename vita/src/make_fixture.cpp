// Self-authored Allegrex instructions; no commercial game data.
#include "analysis.hpp"
#include "emitter.hpp"
#include <stdexcept>
namespace psprecomp {
std::int32_t runtime_thread_uid() noexcept { return -1; }
const char *runtime_thread_name() noexcept { return "fixture"; }
std::uint32_t runtime_dispatch_pc() noexcept { return 0; }
}
int main(int argc, char **argv) {
    if (argc != 2) return 2;
    psprecomp::GuestMemory memory;
    // A saves $ra, calls sceKernelDelayThread(2000), returns 7, restores stack.
    const std::uint32_t a[] = {0x27bdfff0,0xafbf000c,0x240407d0,
        0x0e200c00,0,0x24020007,0x8fbf000c,0x03e00008,0x27bd0010};
    const std::uint32_t b[] = {0x24020009,0x03e00008,0};
    for (unsigned i=0;i<std::size(a);++i) memory.store32(0x08801000+i*4,a[i]);
    for (unsigned i=0;i<std::size(b);++i) memory.store32(0x08802000+i*4,b[i]);
    mhf::psp::recomp::ProgramInput input;
    input.memory=&memory;
    input.code={{0x08801000,0x08801000+sizeof(a)},{0x08802000,0x08802000+sizeof(b)}};
    input.extra_entries={0x08801000,0x08802000};
    input.imports={{"ThreadManForUser",0xCEADEB47,0x08803000}};
    auto analysis=mhf::psp::recomp::analyze(input);
    if (analysis.functions.size()!=2 || analysis.unresolved_jumps) throw std::runtime_error("fixture analysis failed");
    mhf::psp::recomp::EmitOptions options;
    options.program_name="pocket_fixture"; options.entry=0x08801000; options.parts=1;
    options.code_start=0x08801000; options.code_end=0x08803008;
    (void)mhf::psp::recomp::emit_program(input,analysis,options,argv[1]);
}
