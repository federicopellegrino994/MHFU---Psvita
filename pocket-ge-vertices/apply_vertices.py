#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path.cwd();hpath=root/'platforms/psp/gpu/ge.hpp';spath=root/'platforms/psp/gpu/ge.cpp'
h=hpath.read_text();s=spath.read_text()
if 'POCKET_VERTEX_REUSE_V6' in h:
    raise SystemExit('V6 già applicata: nessun file modificato.')
if 'render_state_generation_' not in h or 'texture_cache_generation_' not in h:
    raise SystemExit('Richiesta v5: nessun file modificato.')
if 'POCKET_GE_SHADER_STACK' not in (root/'platforms/psp/runtime/src/hle_ge.cpp').read_text():
    raise SystemExit('Manca la correzione stack: nessun file modificato.')
field='    Backend *backend_{};'
vertices='''    std::vector<Vertex> verts;
    verts.reserve(count);'''
timer='''    static const bool timed = std::getenv("MHF_INTERP_LOG") != nullptr;'''
for text,anchor in [(h,field),(s,vertices),(s,timer)]:
    if text.count(anchor)!=1:
        raise SystemExit('Sorgente GE diverso: nessun file modificato. Invia ge.cpp/ge.hpp.')
start=s.find('Ge::DecodedVertex Ge::decode_vertex(')
end=s.find('\nvoid Ge::light(',start)
if start<0 or end<0:
    raise SystemExit('Decoder GE diverso: nessun file modificato.')
fragment=s[start:end]
layout='    const Layout l = layout_for(vt);'
if fragment.count(layout)!=1:
    raise SystemExit('Layout GE diverso: nessun file modificato.')
fragment=fragment.replace(layout,'''    // The vertex layout depends only on VTYPE, not on matrices, bone weights
    // or vertex data. Decode/morph/transform still run on every vertex.
    if (decoded_layout_type_ != vt) {
        const Layout calculated = layout_for(vt);
        decoded_layout_ = {calculated.weight_offset, calculated.tc_offset,
            calculated.color_offset, calculated.normal_offset, calculated.pos_offset, calculated.size};
        decoded_layout_type_ = vt;
    }
    const Layout l{decoded_layout_[0], decoded_layout_[1], decoded_layout_[2],
                   decoded_layout_[3], decoded_layout_[4], decoded_layout_[5]};''',1)
s=s[:start]+fragment+s[end:]
h=h.replace(field,'''    // POCKET_VERTEX_REUSE_V6: per-GE scratch, owned only by its renderer thread.
    std::vector<Vertex> vertex_scratch_;
    mutable std::uint32_t decoded_layout_type_ = ~std::uint32_t(0);
    mutable std::array<unsigned, 6> decoded_layout_{};
'''+field,1)
s=s.replace(vertices,'''    auto &verts = vertex_scratch_;
    verts.clear();
    verts.reserve(count);''',1)
s=s.replace('    // Timing the drawing commands costs a clock read each; only when the interpolation log is on.', '    // Sample vertex/assembly times with a native backend; full timing remains opt-in.')
s=s.replace(timer,'''    static const bool full_timing = std::getenv("MHF_INTERP_LOG") != nullptr;
    const bool timed = full_timing || (backend_ != nullptr && (stats_.primitives & 63u) == 0);
    const std::uint64_t timing_scale = full_timing ? 1u : 64u;
#if defined(__vita__)
    if (stats_.primitives == 1)
        std::fprintf(stderr, "[ge] vertex reuse v6 active; vertex timers %s\\n",
                     full_timing ? "full" : "estimated 1/64 draws");
#endif''',1)
for expr in ['t_vertices - t_start','t_end - t_vertices']:
    old='std::chrono::duration_cast<std::chrono::nanoseconds>('+expr+').count());'
    new='std::chrono::duration_cast<std::chrono::nanoseconds>('+expr+').count()) * timing_scale;'
    if s.count(old)!=1:raise SystemExit('Timer GE diverso: nessun file modificato.')
    s=s.replace(old,new,1)
for p in [hpath,spath]:
    if p.with_name(p.name+'.before-vertices-v6').exists():raise SystemExit('Backup già presente: nessun file modificato.')
for p in [hpath,spath]:shutil.copy2(p,p.with_name(p.name+'.before-vertices-v6'))
hpath.write_text(h);spath.write_text(s)
print('V6 applicata: buffer vertici riutilizzato, layout VTYPE memorizzato e misure GE campionate.')
