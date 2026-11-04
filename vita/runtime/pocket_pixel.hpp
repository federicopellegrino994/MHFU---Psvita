#pragma once
#include <cstdint>
namespace pocket {
inline std::uint32_t unpack_pixel(const std::uint8_t *p, unsigned format) {
    const unsigned v = unsigned(p[0]) | (unsigned(p[1]) << 8);
    unsigned r, g, b;
    switch (format) {
    case 0: r=(v&31)*255/31; g=((v>>5)&63)*255/63; b=((v>>11)&31)*255/31; break;
    case 1: r=(v&31)*255/31; g=((v>>5)&31)*255/31; b=((v>>10)&31)*255/31; break;
    case 2: r=(v&15)*17; g=((v>>4)&15)*17; b=((v>>8)&15)*17; break;
    default: r=p[0]; g=p[1]; b=p[2]; break;
    }
    return 0xff000000u | r | (g<<8) | (b<<16);
}
}
