// Diagnostic presentation of the existing software-rendered PSP framebuffer.
// No GPU acceleration or increase of the guest rendering resolution.
#include "vita_present.hpp"
#include "pocket_pixel.hpp"
#include "mhf/psp/host.hpp"
#include <psp2/display.h>
#include <psp2/kernel/sysmem.h>
#include <algorithm>
#include <cstdio>
namespace {
struct Screen {
    static constexpr unsigned size = 2u*1024u*1024u;
    SceUID block = -1;
    std::uint8_t *base = nullptr;
    unsigned draw = 0;
    bool failed = false;
    unsigned shown = 0;
    bool initialize() {
        if (base) return true;
        if (failed) return false;
        block = sceKernelAllocMemBlock("pocket-software-display", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW, 2*size, nullptr);
        if (block < 0 || sceKernelGetMemBlockBase(block, reinterpret_cast<void **>(&base)) < 0) {
            std::fprintf(stderr,"[present] display allocation failed\n"); failed=true; return false;
        }
        std::fill_n(reinterpret_cast<std::uint32_t *>(base), (2*size)/4, 0xff000000u);
        return true;
    }
    void show(mhf::psp::Runtime &rt, unsigned address, unsigned stride, unsigned format) {
        if (!address || format>3 || stride<480 || stride>2048 || !initialize()) return;
        const unsigned bytes = format==3 ? 4 : 2;
        const std::size_t length = (std::size_t(stride)*271+480)*bytes;
        const auto *src = rt.memory().raw_pointer(address,length);
        if (!src) { std::fprintf(stderr,"[present] invalid framebuffer range\n"); return; }
        auto *dst = reinterpret_cast<std::uint32_t *>(base+draw*size);
        for (unsigned y=0;y<272;++y) {
            auto *line=dst+(y*2)*960;
            const auto *input=src+std::size_t(y)*stride*bytes;
            for (unsigned x=0;x<480;++x) {
                const auto color=pocket::unpack_pixel(input+x*bytes,format);
                line[x*2]=line[x*2+1]=color;
            }
            std::copy_n(line,960,line+960);
        }
        SceDisplayFrameBuf frame{};
        frame.size=sizeof(frame); frame.base=dst; frame.pitch=960;
        frame.pixelformat=SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width=960; frame.height=544;
        const int result=sceDisplaySetFrameBuf(&frame,SCE_DISPLAY_SETBUF_NEXTFRAME);
        if (result<0) { std::fprintf(stderr,"[present] SetFrameBuf error %08X\n",unsigned(result)); return; }
        // Latch this buffer before reusing the previous one. GE calls us serially.
        sceDisplayWaitVblankStart();
        draw ^= 1;
        if (++shown==1 || shown%10==0)
            std::fprintf(stderr,"[present] submitted %u frames; PSP format %u stride %u; 2x nearest scaling\n",shown,format,stride);
    }
};
}
// Optional weak hook in hle_ge.cpp, executed by the GE worker in present order.
extern "C" void pocket_software_present(mhf::psp::Runtime *rt, std::uint32_t address,
                                        std::uint32_t stride, std::uint32_t format) {
    static Screen screen;
    screen.show(*rt,address,stride,format);
}
void pocket_update_input(mhf::psp::Runtime &rt, const SceCtrlData &pad) {
    mhf::psp::ControllerState state{};
    using namespace mhf::psp::buttons;
    struct Pair { unsigned vita, psp; };
    const Pair mapping[] = {
        {SCE_CTRL_SELECT,kSelect},{SCE_CTRL_START,kStart},{SCE_CTRL_UP,kUp},
        {SCE_CTRL_RIGHT,kRight},{SCE_CTRL_DOWN,kDown},{SCE_CTRL_LEFT,kLeft},
        {SCE_CTRL_LTRIGGER,kLTrigger},{SCE_CTRL_RTRIGGER,kRTrigger},
        {SCE_CTRL_TRIANGLE,kTriangle},{SCE_CTRL_CIRCLE,kCircle},
        {SCE_CTRL_CROSS,kCross},{SCE_CTRL_SQUARE,kSquare}
    };
    for (const auto &m:mapping) if (pad.buttons&m.vita) state.buttons|=m.psp;
    // POCKET_RIGHT_STICK_DPAD_V1: right stick adds PSP D-pad buttons.
    // Engage beyond 80/176; release at 104/152 to prevent threshold chatter.
    // One state per axis, so the stick itself never sends opposite directions.
    static int right_x = 0, right_y = 0;
    const auto axis = [](unsigned value, int previous) {
        if (value <= 80) return -1;
        if (value >= 176) return 1;
        if (previous < 0 && value < 104) return -1;
        if (previous > 0 && value > 152) return 1;
        return 0;
    };
    right_x = axis(pad.rx, right_x);
    right_y = axis(pad.ry, right_y);
    if (right_x < 0) state.buttons |= kLeft;
    if (right_x > 0) state.buttons |= kRight;
    if (right_y < 0) state.buttons |= kUp;
    if (right_y > 0) state.buttons |= kDown;
    static bool input_logged = false;
    if (!input_logged) {
        std::fprintf(stderr, "[input] right stick -> PSP D-pad; deadzone and hysteresis active\n");
        input_logged = true;
    }
    state.lx=pad.lx; state.ly=pad.ly;
    mhf::psp::set_controller_state(rt,state);
}
