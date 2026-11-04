#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path.cwd();hp=root/'platforms/psp/gpu/ge.hpp';sp=root/'platforms/psp/gpu/ge.cpp';wp=root/'platforms/psp/runtime/src/hle_ge.cpp'
if not all(p.is_file() for p in (hp,sp,wp)):raise SystemExit('Esegui da ~/mhfu-vita: nessun file modificato.')
h=hp.read_text();s=sp.read_text();w=wp.read_text()
if 'POCKET_GE_CENSUS_V1' in h:raise SystemExit('Censimento già applicato: nessun file modificato.')
if 'POCKET_VERTEX_PIPELINE_V7' not in h or 'POCKET_GE_SHADER_STACK' not in w:raise SystemExit('Richiesti v7 e fix stack GE: nessun file modificato.')
def change(text,old,new):
    if text.count(old)!=1:raise SystemExit('Sorgente diverso: nessun file modificato. Invia ge.cpp, ge.hpp e hle_ge.cpp.')
    return text.replace(old,new,1)
h=change(h,'#include "backend.hpp"','#include "backend.hpp"\n#include "pocket_geometry_probe.hpp"')
h=change(h,'    void set_backend(Backend *backend) noexcept { backend_ = backend; }','    void set_backend(Backend *backend) noexcept { backend_ = backend; }\n    void geometry_probe_present() { geometry_probe_.present(); }')
h=change(h,'    Backend *backend_{};','    // POCKET_GE_CENSUS_V1: owned by the GE worker.\n    GeometryProbe geometry_probe_;\n    Backend *backend_{};')
s=change(s,'''    const ListResult result = run_commands(pc, stall, on_event);
    if (backend_ != nullptr) backend_->flush();''','''    const bool probe_list = backend_ != nullptr && geometry_probe_.active();
    const auto probe_start = probe_list ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
    const ListResult result = run_commands(pc, stall, on_event);
    if (backend_ != nullptr) backend_->flush();
    if (probe_list) geometry_probe_.add_list(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-probe_start).count()));''')
a=s.find('    static const bool full_timing =',s.index('void Ge::draw_primitive('));b=s.find('    auto &verts = vertex_scratch_;',a)
if a<0 or b<0 or 'vertex_timer_rng_' not in s[a:b] or 'const auto t_start' not in s[a:b]:raise SystemExit('Timer inattesi: nessun file modificato.')
s=s[:a]+'''    const auto t_start = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
'''+s[b:]
prelude='''    static const bool full_timing = std::getenv("MHF_INTERP_LOG") != nullptr;
    const bool probe_draw = backend_ != nullptr && geometry_probe_.active();
    const bool timed = full_timing || probe_draw;
    const std::uint64_t timing_scale = full_timing ? 1u : 30u;
    if (probe_draw) {
        const unsigned flags = (regs_[kLightingEnable]&1u) | ((regs_[kTexMapMode]&3u)<<1) |
            ((regs_[kTextureMapEnable]&1u)<<3) | ((regs_[kFogEnable]&1u)<<4) | ((regs_[kClearMode]&1u)<<5);
        geometry_probe_.record(vt,type,count,vsize,flags);
    }
#if defined(__vita__)
    if (stats_.primitives == 1)
        std::fprintf(stderr,"[ge] census v1 installed; sampling 1/30 present intervals unless disabled\\n");
#endif
    const auto t_info = timed ? std::chrono::steady_clock::now() : std::chrono::steady_clock::time_point{};
'''
s=change(s,'    stats_.vertices += count;\n','    stats_.vertices += count;\n'+prelude)
s=s.replace('    // Timing the drawing commands costs a clock read each; only when the interpolation log is on.\n','').replace('    // Sample vertex/assembly times with a native backend; full timing remains opt-in.\n','')
s=change(s,'''    const auto t_end = std::chrono::steady_clock::now();
    stats_.transform_ns''','''    const auto t_end = std::chrono::steady_clock::now();
    if (probe_draw) geometry_probe_.add_phases(
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t_start-t_info).count()),
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t_vertices-t_start).count()),
        static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t_end-t_vertices).count()));
    stats_.transform_ns''')
w=change(w,'                    backend->present(p.address, p.stride, p.format);','                    renderer().geometry_probe_present();\n                    backend->present(p.address, p.stride, p.format);')
extra=hp.with_name('pocket_geometry_probe.hpp');payload=Path(__file__).with_name(extra.name)
if not payload.is_file() or extra.exists():raise SystemExit('Header assente o già presente: nessun file modificato.')
for p in (hp,sp,wp):
    if p.with_name(p.name+'.before-census-v1').exists():raise SystemExit('Backup già presente: nessun file modificato.')
for p in (hp,sp,wp):shutil.copy2(p,p.with_name(p.name+'.before-census-v1'))
shutil.copy2(payload,extra);hp.write_text(h);sp.write_text(s);wp.write_text(w)
print('Censimento grafico applicato. Misure CPU e formati ogni 30 presentazioni. Non è una patch FPS.')
