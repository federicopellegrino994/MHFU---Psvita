#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
namespace mhf::psp::gpu {
// POCKET_GE_CENSUS_V1: all fields belong to the GE worker.
class GeometryProbe {
    struct Bucket { unsigned vt{}, flags{}, stride{}; std::uint64_t draws{}, vertices{}, ns{}; };
    std::array<Bucket,64> buckets_{};
    unsigned used_{}, current_=64;
    std::uint64_t frames_{}, draws_{}, vertices_{}, small_{}, overflow_{}, simple_{};
    std::array<std::uint64_t,8> primitives_{};
    std::uint64_t lists_{}, info_{}, verts_{}, assembly_{};
    bool enabled_=true, active_=true;
public:
    GeometryProbe() {
#if defined(__vita__)
        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-census.flag","rb")) {
            std::fclose(f); enabled_=active_=false;
            std::fprintf(stderr,"[ge-census] disabled by no-census.flag\n");
        }
#endif
    }
    bool active() const noexcept {return active_;}
    void record(unsigned vt,unsigned prim,unsigned count,unsigned stride,unsigned flags) {
        if(!active_)return;
        ++draws_;vertices_+=count;small_+=count<=8;++primitives_[prim&7];
        if(!(vt&(1u<<23))&&!((vt>>9)&3)&&!((vt>>18)&7)&&!(flags&0x27)&&prim>=3&&prim<=5)simple_+=count;
        if(current_<used_){auto &b=buckets_[current_];if(b.vt==vt&&b.flags==flags&&b.stride==stride){++b.draws;b.vertices+=count;return;}}
        for(unsigned i=0;i<used_;++i){auto &b=buckets_[i];if(b.vt==vt&&b.flags==flags&&b.stride==stride){current_=i;++b.draws;b.vertices+=count;return;}}
        if(used_==64){current_=64;++overflow_;return;}
        current_=used_++;buckets_[current_]={vt,flags,stride,1,count,0};
    }
    void add_list(std::uint64_t ns){if(active_)lists_+=ns;}
    void add_phases(std::uint64_t i,std::uint64_t v,std::uint64_t a){
        if(!active_)return;
        info_+=i;verts_+=v;assembly_+=a;if(current_<used_)buckets_[current_].ns+=v;
    }
    void present(){
        if(!enabled_)return;
        ++frames_;
        if(active_){
            const auto u=[](std::uint64_t n){return static_cast<unsigned>(n);};
            std::fprintf(stderr,"[ge-census] frame=%u draws=%u vertices=%u small_draws_le8=%u simple3d_vertices=%u overflow_draws=%u\n",u(frames_),u(draws_),u(vertices_),u(small_),u(simple_),u(overflow_));
            std::fprintf(stderr,"[ge-census-time] measured CPU wall ms: lists=%.3f info=%.3f vertices=%.3f assembly=%.3f; includes instrumentation; excludes present\n",lists_/1e6,info_/1e6,verts_/1e6,assembly_/1e6);
            std::fprintf(stderr,"[ge-census-prims] points=%u lines=%u line_strip=%u triangles=%u tri_strip=%u tri_fan=%u sprites=%u other=%u\n",u(primitives_[0]),u(primitives_[1]),u(primitives_[2]),u(primitives_[3]),u(primitives_[4]),u(primitives_[5]),u(primitives_[6]),u(primitives_[7]));
            std::array<unsigned,64> order{};for(unsigned i=0;i<used_;++i)order[i]=i;
            std::sort(order.begin(),order.begin()+used_,[&](unsigned a,unsigned b){return buckets_[a].vertices>buckets_[b].vertices;});
            for(unsigned i=0;i<std::min(used_,12u);++i){const auto &b=buckets_[order[i]];const unsigned vt=b.vt;
                std::fprintf(stderr,"[ge-census-format] vtype=%06X stride=%u through=%u weight_fmt=%u weights=%u morphs=%u pos_fmt=%u normal_fmt=%u uv_fmt=%u color_fmt=%u index_fmt=%u lighting=%u texgen=%u textured=%u fog=%u clear=%u draws=%u vertices=%u vertex_ms=%.3f\n",vt,b.stride,(vt>>23)&1,(vt>>9)&3,((vt>>9)&3)?((vt>>14)&7)+1:0,((vt>>18)&7)+1,(vt>>7)&3,(vt>>5)&3,vt&3,(vt>>2)&7,(vt>>11)&3,b.flags&1,(b.flags>>1)&3,(b.flags>>3)&1,(b.flags>>4)&1,(b.flags>>5)&1,u(b.draws),u(b.vertices),b.ns/1e6);
            }
        }
        active_=frames_%30==0;
        if(active_){buckets_={};used_=0;current_=64;draws_=vertices_=small_=overflow_=simple_=0;primitives_={};lists_=info_=verts_=assembly_=0;}
    }
};
}
