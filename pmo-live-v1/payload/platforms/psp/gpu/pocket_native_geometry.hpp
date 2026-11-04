#pragma once
#include "pocket_native_pmo.hpp"

#include <algorithm>
#include <array>
#include <map>

namespace pocket::pmo {
struct Input {float position[4],color[4],uv[2];};
struct PrimitiveRange {
    unsigned guest_pc{},guest_indices{},command{},first_index{},index_count{};
};
struct Geometry {
    unsigned guest_vertices{},initial_indices{},final_indices{},vertex_type{},cull{},command_words{},input_vertices{};
    std::vector<unsigned> source_addresses;
    std::vector<Input> vertices; // UV normalized, before live scale/offset
    std::vector<std::uint16_t> triangles;
    std::vector<PrimitiveRange> primitives;
    std::array<float,3> low{},high{};
};
// Compile a complete immutable rigid PMO group, rather than caching thousands
// of tiny PRIM requests independently. Unsupported command sequences reject
// the WHOLE group without modifying the caller's output.
template<class Read> bool compile_group(unsigned geometry,unsigned bytes,unsigned start,Read read,Geometry &out) {
    const std::uint64_t end=std::uint64_t(geometry)+bytes;
    if(end>0x100000000ull||start<geometry||start>=end||(start&3))return false;
    const auto inside=[&](unsigned a,unsigned n)->const std::uint8_t*{
        if(a<geometry||std::uint64_t(a)+n>end)return nullptr;
        return read(a,n);
    };
    const auto *begin=inside(start,24);if(!begin)return false;
    if(u32(begin)!=0x14000000u || u32(begin+4)!=0x10000000u ||
        (u32(begin+8)>>24)!=2 || (u32(begin+12)>>24)!=1 ||
        (u32(begin+16)>>24)!=0x12 || (u32(begin+20)>>24)!=0x9b)return false;
    Geometry p;p.vertex_type=u32(begin+16)&0xffffffu;p.cull=u32(begin+20)&0xffffffu;
    if(p.vertex_type!=0x091au&&p.vertex_type!=0x111au)return false;
    const unsigned width=p.vertex_type==0x091au?1u:2u;
    const auto relative=[&](unsigned word){return ((start&0x0fffffffu)+(word&0xffffffu))&0x0fffffffu;};
    p.guest_vertices=relative(u32(begin+12));p.initial_indices=relative(u32(begin+8));
    unsigned ia=p.initial_indices,pc=start+24;
    std::map<unsigned,std::uint16_t> mapped;
    for(unsigned n=0;n<32768;++n,pc+=4) {
        const auto *ptr=inside(pc,4);if(!ptr)return false;
        const unsigned word=u32(ptr);
        if(word==0x13000000u) {
            const auto *ret=inside(pc+4,4);
            if(!ret||u32(ret)!=0x0b000000u||p.primitives.empty())return false;
            p.final_indices=ia;p.command_words=(pc-start)/4u+2u;out=std::move(p);return true;
        }
        if(word>>24!=4)return false;
        const unsigned type=(word>>16)&7u,count=word&65535u;
        if(type<3||type>5||count<3||count>4096||p.triangles.size()>1024u*1024u)return false;
        const auto *ib=inside(ia,count*width);if(!ib)return false;
        std::vector<std::uint16_t> indices;indices.reserve(count);
        for(unsigned i=0;i<count;++i) {
            const unsigned index=width==1?ib[i]:u16(ib+i*2u);
            auto found=mapped.find(index);
            if(found==mapped.end()) {
                if(p.vertices.size()>=65535)return false;
                const std::uint64_t address=std::uint64_t(p.guest_vertices)+index*12u;
                if(address>0xffffffffu)return false;
                const auto *vb=inside(static_cast<unsigned>(address),12);if(!vb)return false;
                Input v{};
                for(unsigned c=0;c<3;++c){std::int16_t q;std::memcpy(&q,vb+6+c*2,2);v.position[c]=float(q)*(1.0f/32768);}
                v.position[3]=1;
                const unsigned color=u16(vb+4);
                for(unsigned c=0;c<4;++c)v.color[c]=float(((color>>(c*4))&15)*17)*(1.0f/255);
                v.uv[0]=float(u16(vb))*(1.0f/32768);v.uv[1]=float(u16(vb+2))*(1.0f/32768);
                for(unsigned c=0;c<3;++c) {
                    if(p.vertices.empty())p.low[c]=p.high[c]=v.position[c];
                    else {p.low[c]=std::min(p.low[c],v.position[c]);p.high[c]=std::max(p.high[c],v.position[c]);}
                }
                found=mapped.emplace(index,static_cast<std::uint16_t>(p.vertices.size())).first;
                p.vertices.push_back(v);p.source_addresses.push_back(static_cast<unsigned>(address));
            }
            indices.push_back(found->second);
        }
        const unsigned first=static_cast<unsigned>(p.triangles.size());
        const auto tri=[&](unsigned a,unsigned b,unsigned c){p.triangles.push_back(indices[a]);p.triangles.push_back(indices[b]);p.triangles.push_back(indices[c]);};
        if(type==3)for(unsigned i=0;i+2<count;i+=3)tri(i,i+1,i+2);
        if(type==4)for(unsigned i=0;i+2<count;++i){if(i&1)tri(i+1,i,i+2);else tri(i,i+1,i+2);}
        if(type==5)for(unsigned i=1;i+1<count;++i)tri(0,i,i+1);
        p.primitives.push_back({pc,ia,word,first,static_cast<unsigned>(p.triangles.size())-first});
        p.input_vertices+=count;ia+=count*width;
    }
    return false;
}
} // namespace pocket::pmo
