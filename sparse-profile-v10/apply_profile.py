#!/usr/bin/env python3
from pathlib import Path
import shutil

root=Path.cwd(); package=Path(__file__).resolve().parent
paths=[root/'platforms/psp/gpu/ge.hpp',root/'platforms/psp/gpu/ge.cpp',root/'vita/runtime/vita_gpu.cpp']
header=root/'platforms/psp/gpu/pocket_sparse_profile.hpp'
if not all(p.is_file() for p in paths):raise SystemExit('Esegui da ~/mhfu-vita. Nessuna modifica.')
texts=[p.read_text() for p in paths]
h,g,b=texts
if 'pocket_sparse_profile.hpp' in h or header.exists():raise SystemExit('Profilo v10 già presente: nessuna modifica.')
if '[gpu-rigid-cull] v9' not in b or 'POCKET_RIGID_GPU_V8' not in h:raise SystemExit('Richiesta la versione v9: nessuna modifica.')
def change(s,a,z):
    if s.count(a)!=1:raise SystemExit('Sorgenti diversi da quelli attesi: nessuna modifica. Invia ge.cpp, ge.hpp e vita_gpu.cpp.')
    return s.replace(a,z,1)
h=change(h,'#include "pocket_geometry_probe.hpp"','#include "pocket_geometry_probe.hpp"\n#include "pocket_sparse_profile.hpp"')
h=change(h,'    void geometry_probe_present() { geometry_probe_.present(); }','''    void geometry_probe_present() {
        geometry_probe_.present();
        render_probe_.present(stats_.commands,stats_.primitives);
    }
    pocket::RenderProbe &render_probe() const { return render_probe_; }''')
h=change(h,'    GeometryProbe geometry_probe_;','    GeometryProbe geometry_probe_;\n    mutable pocket::RenderProbe render_probe_; // POCKET_SPARSE_PROFILE_V10')
g=change(g,'    const ListResult result = run_commands(pc, stall, on_event);','    auto list_measure=render_probe_.scope(pocket::RenderProbe::Lists,0);\n    const ListResult result = run_commands(pc, stall, on_event);')
g=change(g,'void Ge::draw_primitive(std::uint32_t data) {','void Ge::draw_primitive(std::uint32_t data) {\n    auto draw_measure=render_probe_.scope(pocket::RenderProbe::Draw);')
g=change(g,'    static const bool full_timing = std::getenv("MHF_INTERP_LOG") != nullptr;','    auto fallback_measure=render_probe_.scope(pocket::RenderProbe::Fallback);\n    static const bool full_timing = std::getenv("MHF_INTERP_LOG") != nullptr;')
g=change(g,'    auto &verts = vertex_scratch_;','    auto vertices_measure=render_probe_.scope(pocket::RenderProbe::Vertices);\n    auto &verts = vertex_scratch_;')
g=change(g,'    const auto t_vertices = timed ? std::chrono::steady_clock::now() : t_start;','    vertices_measure.stop();\n    auto assembly_measure=render_probe_.scope(pocket::RenderProbe::Assembly);\n    const auto t_vertices = timed ? std::chrono::steady_clock::now() : t_start;')
# Old fine timers are replaced, including the frequent timer pause/resume calls.
b=change(b,': total(v), running(sample), sampled(sample), multiplier(scale) {',': total(v), running(false), sampled(false), multiplier(scale) {\n        (void)sample; // v10: legacy fine timers disabled; sparse probe replaces them')
b=change(b,'    std::uint64_t validated_hash(const Ge &ge, const Ge::TextureInfo &info) {','    std::uint64_t validated_hash(const Ge &ge, const Ge::TextureInfo &info) {\n        auto hash_measure=ge.render_probe().scope(pocket::RenderProbe::Hash,8);')
b=change(b,'        { Cost cost(profile.decode); ge.decode_texture(pixels.data()); }','        { auto decode_measure=ge.render_probe().scope(pocket::RenderProbe::Decode,0); ge.decode_texture(pixels.data()); }')
b=change(b,'            Cost cost(profile.upload);','            auto upload_measure=ge.render_probe().scope(pocket::RenderProbe::Upload,0);')
b=change(b,'    void prepare(const Ge &ge) {','    void prepare(const Ge &ge) {\n        auto state_measure=ge.render_probe().scope(pocket::RenderProbe::Prepare);')
b=change(b,'    bool draw_rigid(const Ge &ge, unsigned type, unsigned count, unsigned vaddr, unsigned iaddr) override {','    bool draw_rigid(const Ge &ge, unsigned type, unsigned count, unsigned vaddr, unsigned iaddr) override {\n        auto rigid_measure=ge.render_probe().scope(pocket::RenderProbe::Rigid);')
b=change(b,'        if (!rigid_transform_valid || generation != rigid_generation) {','        if (!rigid_transform_valid || generation != rigid_generation) {\n            auto matrix_measure=ge.render_probe().scope(pocket::RenderProbe::MatrixBuild,8);')
b=change(b,'        rigid_scratch.resize(count);','        auto validate_measure=ge.render_probe().scope(pocket::RenderProbe::Validate);\n        rigid_scratch.resize(count);')
b=change(b,'        // No GL or batch mutation occurred before ALL vertices were validated.','        validate_measure.stop();\n        auto submit_measure=ge.render_probe().scope(pocket::RenderProbe::Submit);\n        // No GL or batch mutation occurred before ALL vertices were validated.')
b=change(b,'[gpu-cost-est] ms/frame (setup/hash/submit sampled 1/64):','[gpu-cost-legacy-disabled] use render-v10-time; old counters:')
b=change(b,'fine timers sampled 1/64','fine timers replaced by render-v10')
b=change(b,'        rigid_scratch.reserve(256);','        std::fprintf(stderr,"[render-v10] sparse profiling installed; old fine timers disabled; no-sparse-profile.flag disables new probe\\n");\n        rigid_scratch.reserve(256);')
for p in paths:
    if p.with_name(p.name+'.before-profile-v10').exists():raise SystemExit('Backup v10 già presente: nessuna modifica.')
payload=(package/'pocket_sparse_profile.hpp').read_bytes()
for p in paths:shutil.copy2(p,p.with_name(p.name+'.before-profile-v10'))
for p,s in zip(paths,(h,g,b)):p.write_text(s)
header.write_bytes(payload)
print('Profilo v10 applicato. Diagnostica: nessun aumento FPS promesso. Backup .before-profile-v10 creati.')
