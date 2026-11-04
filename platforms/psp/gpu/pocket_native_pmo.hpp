#pragma once
// EU ULES01213 v1.01: native planning of pmo::drawMesh (0886171C).
// No guest writes, GL calls, cache or runtime installation in this module.
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>

namespace pocket::pmo {
struct Call {
    unsigned group{}, material{}, texture{}, geometry_address{}, command_offset{};
};
struct Plan {std::vector<std::uint32_t> commands;std::vector<Call> calls;};
inline std::uint32_t u32(const std::uint8_t*p){std::uint32_t v;std::memcpy(&v,p,4);return v;}
inline unsigned u16(const std::uint8_t*p){std::uint16_t v;std::memcpy(&v,p,2);return v;}
inline std::uint32_t rgb(const std::uint8_t*p){return unsigned(p[0])|(unsigned(p[1])<<8)|(unsigned(p[2])<<16);}
// Read returns null unless the whole guest range is mapped. Table references
// must stay inside the PMO's pre-geometry region. The live material/lighting
// tables and texture fragments can be allocated independently of that region.
template<class Read> bool draw_mesh(unsigned self,unsigned tmh,unsigned mesh_arg,Read read,Plan&out) {
    const auto *obj=read(self,32);if(!obj)return false;
    const auto base=u32(obj),geometry=u32(obj+4),materials=u32(obj+8),lighting=u32(obj+12);
    const auto *h=read(base,56);
    if(!h||u32(h)!=0x006f6d70u||u32(h+4)!=0x00302e31u)return false;
    const auto size=u32(h+8),geometry_offset=u32(h+52);
    if(geometry_offset<56||geometry_offset>size||geometry_offset>1024u*1024u||size>16u*1024u*1024u)return false;
    const unsigned mesh=mesh_arg&255u;
    if(mesh>=u16(h+28))return false;
    const auto table=[&](std::uint64_t offset,unsigned bytes)->const std::uint8_t*{
        if(offset>geometry_offset||bytes>geometry_offset-offset||base>0xffffffffu-offset)return nullptr;
        return read(static_cast<unsigned>(base+offset),bytes);
    };
    const auto *mh=table(std::uint64_t(u32(h+32))+mesh*24u,24);
    if(!mh)return false;
    const auto groups=u16(mh+20),first_group=u16(mh+22),first_material=u16(mh+18);
    if(groups>4096)return false;
    if(lighting>0xffffffffu-mesh*8u)return false;
    const auto *lh=read(lighting+mesh*8u,8);if(!lh)return false;
    Plan p;p.commands.reserve(16+groups*20u);p.calls.reserve(groups);
    const auto emit=[&](std::uint32_t word){p.commands.push_back(word);};
    const auto flags=u32(lh),blend=u32(lh+4);
    if(flags&0x80000000u) {
        emit(0x17000000u|(flags&1u));emit(0x1f000000u|((flags>>1)&1u));
        // Match the original's state inheritance: no alpha-disable command
        // is generated when ALPHABLEND is clear.
        if(flags&4u) {
            emit(0x21000001u);emit(blend);
            if(blend==0xdf0000a2u){emit(0x27000001u);emit(0xd8000003u);emit(0xd9000000u);}
        }
    }
    emit(0x48000000u|(u32(mh)>>8));emit(0x49000000u|(u32(mh+4)>>8));
    int previous_texture=-1;
    for(unsigned i=0;i<groups;++i) {
        const auto *g=table(std::uint64_t(u32(h+36))+(first_group+i)*16u,16);if(!g)return false;
        const int relative=static_cast<std::int8_t>(g[0]);
        const std::int64_t remap=std::int64_t(u32(h+40))+first_material+relative;
        if(remap<0||remap>0xffffffffu)return false;
        const auto *mi=table(static_cast<unsigned>(remap),1);if(!mi)return false;
        const unsigned material=*mi;
        // Original loads an unsigned byte; 0xff is not a signed -1 sentinel.
        if(material>=u16(h+30)||materials>0xffffffffu-material*16u)return false;
        const auto *mat=read(materials+material*16u,16);if(!mat)return false;
        if(mat[3]==0)continue;
        emit(0x53000003u);emit(0x56000000u|rgb(mat));emit(0x58000000u|mat[3]);emit(0x55000000u|rgb(mat+4));
        const unsigned texture=mat[8];
        if(texture==255 || int(texture)!=previous_texture) {
            if(texture==255)emit(0x1e000000u);
            else {
                const auto *to=read(tmh,16);if(!to||texture>=to[8])return false;
                const auto fragments=u32(to+4);if(fragments>0xffffffffu-texture*32u)return false;
                const auto *fragment=read(fragments+texture*32u,32);if(!fragment)return false;
                emit(0x1e000001u);emit(0xc2000001u);
                for(unsigned n=0;n<8;++n)emit(u32(fragment+n*4u));
                emit(0xcb000000u);
            }
            previous_texture=int(texture);
        }
        const auto offset=u32(g+4);
        if(offset>=size-geometry_offset||geometry>0xffffffffu-offset||((geometry+offset)&3u))return false;
        const auto address=geometry+offset;
        p.calls.push_back({first_group+i,material,texture,address,static_cast<unsigned>(p.commands.size())});
        emit(0x10000000u|((address&0xff000000u)>>8));
        emit(0x13000000u);emit(0x0a000000u|(address&0xffffffu));
    }
    emit(0x27000000u);
    out=std::move(p);return true;
}
} // namespace pocket::pmo
