#pragma once
#include "rigid_gpu_math.hpp"
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <map>
#include <memory>
#include <vector>

namespace pocket::resident {
// Own the compiled data: guest RAM/VRAM may be overwritten at the same address.
// Exact byte checks include colors, UVs and indices; no address-only identity.
using Vertex = rigid::Input;
struct Packet {
    std::uint64_t id{}, stamp{};
    unsigned maximum{};
    std::vector<std::uint8_t> indices, source;
    std::vector<Vertex> triangles;
    std::array<float,3> low{}, high{};
    rigid::Transform checked{};
    bool checked_valid=false, safe=false;
    std::size_t bytes() const {return indices.size()+source.size()+triangles.size()*sizeof(Vertex);}
    bool accepts(const rigid::Transform &t) {
        if(checked_valid && checked.clip==t.clip && checked.bound==t.bound && checked.viewport==t.viewport)
            return safe;
        checked=t; checked_valid=true; safe=true;
        // Every safe constraint is a strict homogeneous half-space with a
        // conservative convex roundoff bound. If all box corners satisfy it,
        // so do all vertices inside. Never infer visibility from box failure.
        for(unsigned n=0;n<8;++n)
            if(!t.accepts((n&1)?high[0]:low[0],(n&2)?high[1]:low[1],(n&4)?high[2]:low[2])) {
                safe=false;break;
            }
        return safe;
    }
};
struct Cache {
    using Key=std::array<std::uint32_t,10>;
    struct Counts {unsigned hits{}, builds{}, changed{}, denied{}, box_safe{}, box_fallback{};} counts;
    std::map<Key,std::shared_ptr<Packet>> entries;
    std::size_t used{};
    std::uint64_t serial{}, next_id=1;
    static constexpr std::size_t Budget=4u*1024u*1024u;
    static constexpr unsigned MaxSpan=64u*1024u;
    template<class Read> std::shared_ptr<Packet> get(unsigned va,unsigned ia,unsigned count,
        unsigned width,unsigned primitive,bool flat,const std::array<float,4>&uv,Read read) {
        Key key{va,ia,count,width,primitive,unsigned(flat),0,0,0,0};
        for(unsigned i=0;i<4;++i)std::memcpy(&key[6+i],&uv[i],4);
        const auto *idx=read(ia,count*width);
        if(!idx)return {};
        const auto at=[&](unsigned n)->unsigned{
            if(width==1)return idx[n];
            std::uint16_t v;std::memcpy(&v,idx+n*2,2);return v;
        };
        auto found=entries.find(key);
        const bool same_indices=found!=entries.end() && found->second->indices.size()==count*width &&
            std::memcmp(found->second->indices.data(),idx,count*width)==0;
        unsigned maximum=0;
        if(same_indices)maximum=found->second->maximum;
        else for(unsigned i=0;i<count;++i)maximum=std::max(maximum,at(i));
        const unsigned span=(maximum+1u)*12u;
        if(span>MaxSpan){++counts.denied;return {};}
        const auto *src=read(va,span);
        if(!src)return {};
        if(same_indices && found->second->source.size()==span &&
            std::memcmp(found->second->source.data(),src,span)==0) {
            ++counts.hits;found->second->stamp=++serial;return found->second;
        }
        if(found!=entries.end()) {
            used-=found->second->bytes();entries.erase(found);++counts.changed;
        }
        auto p=std::make_shared<Packet>();p->id=next_id++;p->stamp=++serial;p->maximum=maximum;
        p->indices.assign(idx,idx+count*width);p->source.assign(src,src+span);
        std::vector<Vertex> vertices;vertices.reserve(count);
        for(unsigned i=0;i<count;++i) {
            auto v=rigid::decode_111a(src+at(i)*12u,uv[0],uv[1],uv[2],uv[3]);
            for(unsigned c=0;c<3;++c) {
                if(i==0)p->low[c]=p->high[c]=v.position[c];
                else {p->low[c]=std::min(p->low[c],v.position[c]);p->high[c]=std::max(p->high[c],v.position[c]);}
            }
            vertices.push_back(v);
        }
        const auto tri=[&](unsigned a,unsigned b,unsigned c) {
            for(unsigned n:{a,b,c}) {
                auto v=vertices[n];
                if(flat)std::copy_n(vertices[c].color,4,v.color);
                p->triangles.push_back(v);
            }
        };
        p->triangles.reserve((primitive==3?count/3:count-2)*3u);
        if(primitive==3)for(unsigned i=0;i+2<count;i+=3)tri(i,i+1,i+2);
        if(primitive==4)for(unsigned i=0;i+2<count;++i){if(i&1)tri(i+1,i,i+2);else tri(i,i+1,i+2);}
        if(primitive==5)for(unsigned i=1;i+1<count;++i)tri(0,i,i+1);
        while(!entries.empty() && (used+p->bytes()>Budget || entries.size()>=2048)) {
            auto old=std::min_element(entries.begin(),entries.end(),[](const auto&a,const auto&b){return a.second->stamp<b.second->stamp;});
            used-=old->second->bytes();entries.erase(old);
        }
        if(p->bytes()>Budget){++counts.denied;return {};}
        used+=p->bytes();entries.emplace(key,p);++counts.builds;return p;
    }
};
} // namespace pocket::resident
