#pragma once
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>

namespace pocket {
// GE worker only. Inclusive, independently sampled scopes; NEVER add nested
// rows together. Estimates are Horvitz-Thompson sums, not exact CPU timings.
template<class Clock = std::chrono::steady_clock> class BasicRenderProbe {
public:
    enum Kind { Lists, Draw, Rigid, Fallback, Vertices, Assembly,
                MatrixBuild, Validate, Submit, Prepare, Hash, Decode, Upload, Count };
    struct Bucket { std::uint64_t calls{}, samples{}, ns{}; };
private:
    std::array<Bucket, Count> buckets_{};
    std::array<std::uint32_t, Count> random_{};
    bool enabled_ = true, started_ = false;
    unsigned frames_ = 0;
    typename Clock::time_point last_{};
    static constexpr const char *names_[Count] = {
        "lists_inclusive", "draw_inclusive", "rigid_inclusive", "fallback_inclusive",
        "fallback_vertices", "fallback_assembly", "rigid_matrix_build",
        "rigid_validate_copy", "rigid_prepare_submit", "state_prepare_inclusive",
        "texture_validation_hash", "texture_decode", "texture_upload"};
public:
    explicit BasicRenderProbe(bool enabled = true) : enabled_(enabled) {
        for(unsigned i=0;i<Count;++i) random_[i]=0x9e3779b9u*(i+1u);
#if defined(__vita__)
        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-sparse-profile.flag","rb")) {
            std::fclose(f); enabled_=false;
        }
#endif
    }
    bool enabled() const { return enabled_; }
    const Bucket &bucket(Kind kind) const { return buckets_[kind]; }
    class Scope {
        BasicRenderProbe *probe_;
        Kind kind_;
        unsigned shift_;
        bool sampled_ = false;
        typename Clock::time_point begin_{};
    public:
        Scope(BasicRenderProbe &p, Kind k, unsigned shift) : probe_(&p),kind_(k),shift_(shift) {
            if(!p.enabled_)return;
            ++p.buckets_[k].calls;
            auto &r=p.random_[k]; r^=r<<13; r^=r>>17; r^=r<<5;
            sampled_=(r&((1u<<shift)-1u))==0;
            if(sampled_)begin_=Clock::now();
        }
        Scope(const Scope&)=delete;
        Scope&operator=(const Scope&)=delete;
        void stop() {
            if(!sampled_)return;
            const auto end=Clock::now();
            const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(end-begin_).count();
            auto &b=probe_->buckets_[kind_];++b.samples;
            if(ns>0)b.ns+=static_cast<std::uint64_t>(ns)*(std::uint64_t(1)<<shift_);
            sampled_=false;
        }
        ~Scope(){stop();}
    };
    Scope scope(Kind k, unsigned shift=11){return Scope(*this,k,shift);}
    void present(std::uint64_t commands, std::uint64_t draws) {
        if(!enabled_)return;
        const auto now=Clock::now();
        if(!started_){
            long long minimum=0x7fffffffffffffffLL;
            for(unsigned i=0;i<16;++i){
                const auto a=Clock::now(),b=Clock::now();
                const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(b-a).count();
                if(ns<minimum)minimum=ns;
            }
            std::fprintf(stderr,"[render-v10] enabled; clock_pair_min_ns=%u; draw sampling 1/2048; estimates include timer overhead\n",static_cast<unsigned>(minimum));
            last_=Clock::now();started_=true;buckets_={};return;
        }
        ++frames_;
        const double seconds=std::chrono::duration<double>(now-last_).count();
        if(seconds<5)return;
        std::fprintf(stderr,"[render-v10] frames=%u fps=%.2f commands_total=%u draws_total=%u; inclusive estimates, nested rows overlap\n",
            frames_,frames_/seconds,static_cast<unsigned>(commands),static_cast<unsigned>(draws));
        for(unsigned i=0;i<Count;++i){const auto &b=buckets_[i];
            std::fprintf(stderr,"[render-v10-time] %s calls=%u samples=%u est_ms_per_frame=%.3f\n",
                names_[i],static_cast<unsigned>(b.calls),static_cast<unsigned>(b.samples),
                double(b.ns)/(1e6*frames_));
        }
        buckets_={};frames_=0;last_=now;
    }
};
using RenderProbe=BasicRenderProbe<>;
} // namespace pocket
