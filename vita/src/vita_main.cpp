#include "probe.hpp"
#include "stb_truetype.h"
#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/io/stat.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/sysmem.h>
#include <algorithm>
#include <cstdio>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

extern "C" { unsigned int _newlib_heap_size_user = 96u * 1024u * 1024u; }

namespace {
constexpr int width = 960, height = 544, stride = 960;
constexpr unsigned allocation_size = 2u * 1024u * 1024u;
constexpr const char *iso_path = "ux0:data/pocketprobe/game.iso";
constexpr const char *log_path = "ux0:data/pocketprobe/probe.log";
struct Screen {
    SceUID allocation = -1;
    std::uint32_t *pixels = nullptr;
    std::vector<unsigned char> atlas;
    stbtt_bakedchar glyphs[96]{};
    bool init() {
        allocation = sceKernelAllocMemBlock("pocket-display", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                             allocation_size, nullptr);
        if (allocation < 0) return false;
        if (sceKernelGetMemBlockBase(allocation, reinterpret_cast<void **>(&pixels)) < 0) return false;
        SceDisplayFrameBuf frame{};
        frame.size = sizeof(frame); frame.base = pixels; frame.pitch = stride;
        frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width = width; frame.height = height;
        std::fill_n(pixels, stride * height, 0xff181410u);
        if (sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME) < 0) return false;
        std::ifstream font("app0:font.ttf", std::ios::binary);
        if (!font) return false;
        std::vector<unsigned char> bytes((std::istreambuf_iterator<char>(font)), {});
        if (bytes.empty()) return false;
        atlas.resize(512 * 512);
        return stbtt_BakeFontBitmap(bytes.data(), 0, 19.0f, atlas.data(), 512, 512, 32, 96, glyphs) > 0;
    }
    void text(int x, int y, const std::string &text) {
        float pen = static_cast<float>(x);
        for (unsigned char c : text) {
            if (c < 32 || c >= 128) c = '?';
            const auto &g = glyphs[c - 32];
            const int gx = static_cast<int>(pen + g.xoff);
            const int gy = y + static_cast<int>(g.yoff);
            for (int sy = g.y0; sy < g.y1; ++sy)
                for (int sx = g.x0; sx < g.x1; ++sx) {
                    const int dx = gx + sx - g.x0, dy = gy + sy - g.y0;
                    if (dx < 0 || dy < 0 || dx >= width || dy >= height) continue;
                    const unsigned alpha = atlas[sy * 512 + sx];
                    const unsigned v = 24 + (220 * alpha) / 255;
                    pixels[dy * stride + dx] = 0xff000000u | v | (v << 8) | (v << 16);
                }
            pen += g.xadvance;
        }
    }
    void report(const pocket::Report &r) {
        // Hide the buffer while writing; no racing with scanout on refresh.
        sceDisplayWaitVblankStart();
        sceDisplaySetFrameBuf(nullptr, SCE_DISPLAY_SETBUF_IMMEDIATE);
        std::fill_n(pixels, stride * height, 0xff181410u);
        int y = 34;
        for (const auto &line : r.lines) { text(24, y, line); y += 25; }
        text(24, 432, "ISO: ux0:data/pocketprobe/game.iso");
        text(24, 457, "Log: ux0:data/pocketprobe/probe.log");
        text(24, 482, "TRIANGLE: repeat checks    START + SELECT: exit");
        SceDisplayFrameBuf frame{};
        frame.size = sizeof(frame); frame.base = pixels; frame.pitch = stride;
        frame.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
        frame.width = width; frame.height = height;
        sceDisplaySetFrameBuf(&frame, SCE_DISPLAY_SETBUF_NEXTFRAME);
    }
    ~Screen() {
        if (allocation >= 0) {
            sceDisplaySetFrameBuf(nullptr, SCE_DISPLAY_SETBUF_IMMEDIATE);
            sceKernelFreeMemBlock(allocation);
        }
    }
};
}

int main() {
    int exit_code = 0;
    {
        Screen screen;
        if (!screen.init()) { sceKernelExitProcess(1); return 1; }
        sceIoMkdir("ux0:data/pocketprobe", 0777);
#if defined(POCKET_FULL_RUNTIME)
        if (std::freopen("ux0:data/pocketprobe/runtime.log", "w", stderr))
            std::setvbuf(stderr, nullptr, _IONBF, 0);
#endif
        sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
        auto run = [&] {
            pocket::Report pending;
            pending.lines.emplace_back("POCKET PROBE - running fiber and scheduler checks...");
            screen.report(pending);
            {
                std::ofstream progress(log_path, std::ios::trunc);
                progress << "stage=before-fiber-check\n";
            }
            auto r = pocket::inspect(iso_path);
            std::ofstream log(log_path, std::ios::trunc);
            if (log) {
                log << "pointer_bits=" << sizeof(void *) * 8 << "\n";
                for (const auto &line : r.lines) log << line << '\n';
                log.flush();
            }
            if (!log) r.lines.emplace_back("WARN: could not write diagnostic log");
            screen.report(r);
        };
        run();
        unsigned previous = 0;
        while (true) {
            SceCtrlData pad{};
            if (sceCtrlPeekBufferPositive(0, &pad, 1) > 0) {
                if ((pad.buttons & (SCE_CTRL_START | SCE_CTRL_SELECT)) ==
                    (SCE_CTRL_START | SCE_CTRL_SELECT)) break;
                if ((pad.buttons & SCE_CTRL_TRIANGLE) && !(previous & SCE_CTRL_TRIANGLE)) run();
                char status[120];
                std::snprintf(status, sizeof(status), "Buttons %08lx   Left %03u,%03u   Right %03u,%03u    ",
                    static_cast<unsigned long>(pad.buttons), pad.lx, pad.ly, pad.rx, pad.ry);
                // Update a separate status row once per vertical blank.
                sceDisplayWaitVblankStart();
                std::fill_n(screen.pixels + 499 * stride, 30 * stride, 0xff181410u);
                screen.text(24, 520, status);
                previous = pad.buttons;
            } else sceDisplayWaitVblankStart();
        }
    }
    sceKernelExitProcess(exit_code);
    return exit_code;
}
