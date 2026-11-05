#pragma once
#include "rigid_gpu_math.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace pocket::rigid {
// POCKET_VERTEX_VISIBILITY_CACHE_V11
// Cache the result of classify(), never guest pointers or texture/color data.
// Read the actual six position bytes on EVERY lookup. Address reuse and guest
// CPU/DMA writes therefore cannot leave a stale entry. All inputs to classify
// are compared bit-for-bit when the transform is rebuilt.
class VisibilityCache {
    struct Entry {
        std::uint32_t epoch{}, xy{};
        std::uint16_t z{};
        std::uint8_t visibility{}, padding{};
    };
    static constexpr unsigned Capacity=4096;
    std::array<Entry,Capacity> entries_{};
    Transform transform_{};
    bool have_transform_=false, enabled_=true;
    std::uint32_t epoch_=0;
    std::uint64_t hits_=0, misses_=0;
public:
    static unsigned slot(std::uint32_t xy,std::uint16_t z) {
        std::uint32_t h=xy^(std::uint32_t(z)*0x9e3779b9u);
        h^=h>>16;h*=0x85ebca6bu;h^=h>>13;
        return h&(Capacity-1);
    }
    void enabled(bool value){enabled_=value;}
    bool enabled()const{return enabled_;}
    bool set_transform(const Transform&t) {
        if(have_transform_ &&
           std::memcmp(transform_.clip.data(),t.clip.data(),sizeof(t.clip))==0 &&
           std::memcmp(transform_.bound.data(),t.bound.data(),sizeof(t.bound))==0 &&
           std::memcmp(transform_.viewport.data(),t.viewport.data(),sizeof(t.viewport))==0 &&
           transform_.depth_check==t.depth_check) // POCKET_GPU_BATCH_V14
            return false;
        transform_=t;have_transform_=true;
        if(++epoch_==0){entries_={};epoch_=1;}
        return true;
    }
    // p points at the six XYZ bytes, not at the start of the 12-byte vertex.
    Transform::Visibility classify(const std::uint8_t*p) {
        std::uint32_t xy;std::uint16_t z;
        std::memcpy(&xy,p,4);std::memcpy(&z,p+4,2);
        auto &e=entries_[slot(xy,z)];
        if(enabled_ && have_transform_ && e.epoch==epoch_ && e.xy==xy && e.z==z){
            ++hits_;return static_cast<Transform::Visibility>(e.visibility);
        }
        // Derive the calculation from the SAME bytes used as the cache key.
        std::int16_t xyz[3];std::memcpy(xyz,&xy,4);std::memcpy(xyz+2,&z,2);
        const auto v=transform_.classify(float(xyz[0])*(1.0f/32768),
            float(xyz[1])*(1.0f/32768),float(xyz[2])*(1.0f/32768));
        ++misses_;
        if(enabled_ && have_transform_)e={epoch_,xy,z,static_cast<std::uint8_t>(v),0};
        return v;
    }
    std::uint64_t hits()const{return hits_;}
    std::uint64_t misses()const{return misses_;}
    void reset_counts(){hits_=misses_=0;}
};
} // namespace pocket::rigid
