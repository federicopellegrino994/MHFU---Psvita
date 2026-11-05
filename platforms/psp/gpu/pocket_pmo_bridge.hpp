#pragma once
#include "pocket_native_geometry.hpp"
#include <atomic>
#include <cstdio>
#include <exception>
#include <memory>
#include <mutex>
#include <new>

namespace pocket::pmo {
inline bool live_enabled() {
    static const bool enabled=[]{
        if(FILE*f=std::fopen("ux0:data/pocketprobe/no-native-pmo.flag","rb")){std::fclose(f);return false;}
        return true;
    }();
    return enabled;
}
struct Region {unsigned start{},bytes{},header{},self{};bool operator==(const Region&r)const{return start==r.start&&bytes==r.bytes&&header==r.header;}};
// Metadata only crosses the CPU -> GE boundary; GL and compiled geometry stay
// on the GE worker. Reused addresses do not identify immutable content.
struct Registry {
    std::mutex mutex;
    std::map<unsigned,Region> regions;
    void add(const Region&r) {
        std::lock_guard lock(mutex);
        if(regions.size()<4096||regions.contains(r.start))regions[r.start]=r;
    }
    bool get(unsigned start,Region&r) {
        std::lock_guard lock(mutex);auto it=regions.find(start);
        if(it==regions.end())return false;
        r=it->second;return true;
    }
};
inline Registry registry;
inline std::atomic<unsigned> producer_calls{},producer_fallbacks{};
template<class Memory> bool produce(Memory&mem,unsigned self,unsigned tmh,unsigned mesh) {
    if(!live_enabled())return false;
    try {
        Plan plan;
        const auto read=[&](unsigned a,unsigned n){return mem.raw_pointer(a,n);};
        if(!draw_mesh(self,tmh,mesh,read,plan)){++producer_fallbacks;return false;}
        auto *cursor=read(0x089cc000u,4);if(!cursor){++producer_fallbacks;return false;}
        const unsigned begin=u32(cursor),bytes=static_cast<unsigned>(plan.commands.size()*4u);
        if(begin>0xffffffffu-bytes){++producer_fallbacks;return false;}
        auto *dest=read(begin,bytes);if(!dest){++producer_fallbacks;return false;}
        const auto *object=read(self,32);const auto header=u32(object);
        const auto *h=read(header,56);
        const auto geometry=u32(object+4)&0x0fffffffu;
        const auto geometry_bytes=u32(h+8)-u32(h+52);
        const auto groups_offset=u32(h+36),remap_offset=u32(h+40);
        const unsigned total_groups=remap_offset>=groups_offset?(remap_offset-groups_offset)/16u:0;
        for(const auto&call:plan.calls) {
            unsigned next=geometry_bytes;
            if(call.group+1<total_groups) {
                const std::uint64_t address=std::uint64_t(header)+groups_offset+(call.group+1u)*16u;
                const auto *g=address+16u<=std::uint64_t(header)+u32(h+52)?read(static_cast<unsigned>(address),16):nullptr;
                if(!g)continue;
                next=u32(g+4);
            }
            const auto start=call.geometry_address&0x0fffffffu;
            if(start<geometry||next>geometry_bytes||next<=start-geometry||next-(start-geometry)>256u*1024u)continue;
            registry.add({start,next-(start-geometry),header,self});
        }
        // Preserve the original command bytes and final guest list cursor.
        // No custom opcodes, pointers into host memory or GL calls in guest code.
        std::memcpy(dest,plan.commands.data(),bytes);
        const unsigned end=begin+bytes;std::memcpy(cursor,&end,4);
        const unsigned n=++producer_calls;
        if(n<=2 || (n&(n-1))==0)
            std::fprintf(stderr,"[pmo-native] producer calls=%u fallback=%u; EU drawMesh connected\n",n,producer_fallbacks.load());
        return true;
    } catch(const std::exception&) {++producer_fallbacks;return false;}
}

// One entry per COMPLETE group. Exact, contiguous source snapshot validation;
// immutable decode from that snapshot. No eviction scan and no rebuild loop
// at saturation: unsupported/new entries then use the existing GE path.
struct GroupCache {
    // POCKET_PMO_FRAME_CHECK_V12: validated_frame records the frame in which the snapshot was last compared with
    // guest memory. Later uses in the same frame skip the full memcmp; the first use of every frame still compares
    // every byte, so a change made by the game between frames is still detected before the next draw.
    struct Entry {Region region;std::vector<std::uint8_t> source;std::unique_ptr<Geometry> geometry;unsigned changes{};bool rejected{};
        std::uint32_t validated_frame{0xffffffffu};};
    struct Counts {unsigned hits{},builds{},changed{},denied{},unsupported{},gpu{},cpu{},groups{},same_frame{};} counts;
    std::uint32_t frame{0};          // bumped by Ge once per presented frame (GE worker thread)
    bool frame_check{true};          // false with ux0:data/pocketprobe/no-pmo-frame-check.flag
    std::map<unsigned,Entry> entries;
    std::size_t used{};
    bool allocation_blocked=false;
    static constexpr std::size_t Budget=8u*1024u*1024u;
    static std::size_t bytes(const Entry&e) {
        if(!e.geometry)return 0;
        const auto&p=*e.geometry;
        return e.source.capacity()+p.vertices.capacity()*sizeof(Input)+p.triangles.capacity()*2u+
            p.primitives.capacity()*sizeof(PrimitiveRange)+p.source_addresses.capacity()*sizeof(unsigned);
    }
    template<class Read> const Geometry*get(const Region&r,Read read) {
        if(allocation_blocked){++counts.denied;return nullptr;}
        try {return get_impl(r,read);}
        catch(const std::bad_alloc&) {
            // Release optional geometry so the legacy renderer can still
            // allocate its smaller per-primitive scratch after an OOM.
            entries.clear();used=0;allocation_blocked=true;++counts.denied;return nullptr;
        }
    }
    template<class Read> const Geometry*get_impl(const Region&r,Read read) {
        auto it=entries.find(r.start);
        if(it==entries.end()) {
            if(entries.size()>=4096||used>=Budget-256u*1024u){++counts.denied;return nullptr;}
            it=entries.emplace(r.start,Entry{}).first;it->second.region=r;
        }
        auto &e=it->second;
        if(e.region!=r){used-=bytes(e);e=Entry{};e.region=r;}
        if(e.rejected){++counts.denied;return nullptr;}
        if(frame_check && e.geometry && e.validated_frame==frame && e.source.size()==r.bytes){++counts.hits;++counts.same_frame;return e.geometry.get();}
        const auto *live=read(r.start,r.bytes);if(!live)return nullptr;
        if(e.geometry && e.source.size()==r.bytes && std::memcmp(e.source.data(),live,r.bytes)==0){++counts.hits;e.validated_frame=frame;return e.geometry.get();}
        if(e.geometry) {
            used-=bytes(e);e.geometry.reset();std::vector<std::uint8_t>().swap(e.source);++counts.changed;
            if(++e.changes>=4){e.rejected=true;++counts.denied;return nullptr;}
        }
        e.source.assign(live,live+r.bytes);
        auto p=std::make_unique<Geometry>();
        const auto snapshot=[&](unsigned a,unsigned n)->const std::uint8_t*{
            if(a<r.start||std::uint64_t(a)+n>std::uint64_t(r.start)+r.bytes)return nullptr;
            return e.source.data()+(a-r.start);
        };
        if(!compile_group(r.start,r.bytes,r.start,snapshot,*p)){std::vector<std::uint8_t>().swap(e.source);e.rejected=true;++counts.unsupported;return nullptr;}
        e.geometry=std::move(p);
        if(bytes(e)>Budget-used){e.geometry.reset();std::vector<std::uint8_t>().swap(e.source);e.rejected=true;++counts.denied;return nullptr;}
        used+=bytes(e);++counts.builds;e.validated_frame=frame;return e.geometry.get();
    }
};
} // namespace pocket::pmo
