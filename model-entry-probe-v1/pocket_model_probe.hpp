#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace pocket_model_probe {
// Temporary observation only: no replacement, no guest writes, no change to
// registers/PC. One shared set of counters across generated translation units.
inline std::array<std::atomic<unsigned>,4> counts{};
inline constexpr const char *names[]={"pmo.drawMesh","pmo.drawWeight","pmo.drawWeightMesh","pmo.compile"};
inline std::uint32_t u32(const std::uint8_t*p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
inline unsigned u16(const std::uint8_t*p){std::uint16_t v;std::memcpy(&v,p,2);return v;}
template<class Memory> void enter(unsigned id,Memory&mem,std::uint32_t self,
    std::uint32_t a1,std::uint32_t a2,std::uint32_t a3,std::uint32_t ra) {
    if(id>=counts.size())return;
    const unsigned n=counts[id].fetch_add(1,std::memory_order_relaxed)+1u;
    // First eight samples, then powers of two. No clocks or per-call I/O.
    if(n>8 && (n==0 || (n&(n-1))!=0))return;
    std::fprintf(stderr,"[model-entry-v1] %s calls=%u this=%08X a1=%08X a2=%08X a3=%08X ra=%08X totals=%u,%u,%u,%u\n",
        names[id],n,self,a1,a2,a3,ra,
        counts[0].load(std::memory_order_relaxed),counts[1].load(std::memory_order_relaxed),
        counts[2].load(std::memory_order_relaxed),counts[3].load(std::memory_order_relaxed));
    // For draw methods, this is the 32-byte pmo object. Do not infer its layout
    // from ModelBase offsets or read an unchecked guest pointer. Compilation
    // may still have uninitialized members, so inspect that method separately.
    if(id==3)return;
    const auto *obj=mem.raw_pointer(self,32);
    if(!obj){std::fprintf(stderr,"[model-entry-v1] pmo object unmapped\n");return;}
    const auto header=u32(obj),geometry=u32(obj+4),materials=u32(obj+8);
    const auto *h=mem.raw_pointer(header,56);
    if(!h || u32(h)!=0x006f6d70u){
        std::fprintf(stderr,"[model-entry-v1] pmo header unconfirmed header=%08X\n",header);return;
    }
    std::fprintf(stderr,"[model-entry-v1-pmo] this=%08X header=%08X version=%08X bytes=%u meshes=%u material_count_field=%u geometry=%08X materials=%08X mesh_table_offset=%08X groups_offset=%08X remap_offset=%08X bone_offset=%08X material_offset=%08X ge_offset=%08X\n",
        self,header,u32(h+4),u32(h+8),u16(h+28),u16(h+30),geometry,materials,
        u32(h+32),u32(h+36),u32(h+40),u32(h+44),u32(h+48),u32(h+52));
}
} // namespace pocket_model_probe
