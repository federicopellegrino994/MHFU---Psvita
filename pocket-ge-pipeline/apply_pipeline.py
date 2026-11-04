#!/usr/bin/env python3
from pathlib import Path
import shutil

root=Path.cwd()
hp=root/'platforms/psp/gpu/ge.hpp'; sp=root/'platforms/psp/gpu/ge.cpp'
if not hp.is_file() or not sp.is_file():raise SystemExit('Esegui da ~/mhfu-vita: nessun file modificato.')
h=hp.read_text(); s=sp.read_text()
if 'POCKET_VERTEX_PIPELINE_V7' in h:raise SystemExit('V7 già applicata: nessun file modificato.')
if 'POCKET_VERTEX_REUSE_V6' not in h:raise SystemExit('Richiesta v6: nessun file modificato.')

def change(text,old,new):
    if text.count(old)!=1:raise SystemExit('Sorgente GE diverso: nessun file modificato. Invia ge.cpp e ge.hpp.')
    return text.replace(old,new,1)

h=change(h,'    std::vector<Vertex> vertex_scratch_;','''    std::vector<Vertex> vertex_scratch_;
    // POCKET_VERTEX_PIPELINE_V7: reuse only within one immutable draw command.
    struct VertexReuse { Vertex vertex{}; std::uint32_t index{}, epoch{}; };
    std::array<VertexReuse, 256> vertex_reuse_{};
    std::uint32_t vertex_reuse_epoch_{};
    std::uint32_t vertex_timer_rng_ = 0x6d2b79f5u;
    std::uint64_t vertex_decoded_{}, vertex_reused_{}, vertex_indexed_draws_{};
    std::uint64_t vertex_report_decoded_{}, vertex_report_reused_{}, vertex_report_indexed_{};
    unsigned vertex_report_lists_{};
    std::chrono::steady_clock::time_point vertex_report_time_{};''')
s=change(s,'''    if (backend_ != nullptr) backend_->flush();
    return result;''','''    if (backend_ != nullptr) backend_->flush();
#if defined(__vita__)
    if (backend_ != nullptr && (++vertex_report_lists_ & 255u) == 0) {
        const auto now = std::chrono::steady_clock::now();
        if (now - vertex_report_time_ >= std::chrono::seconds(5)) {
            std::fprintf(stderr, "[ge-vertices] period: decoded=%u reused=%u indexed_draws=%u\\n",
                static_cast<unsigned>(vertex_decoded_ - vertex_report_decoded_),
                static_cast<unsigned>(vertex_reused_ - vertex_report_reused_),
                static_cast<unsigned>(vertex_indexed_draws_ - vertex_report_indexed_));
            vertex_report_decoded_ = vertex_decoded_;
            vertex_report_reused_ = vertex_reused_;
            vertex_report_indexed_ = vertex_indexed_draws_;
            vertex_report_time_ = now;
        }
    }
#endif
    return result;''')
# Fast scalar reads only when ALL morph records fit within one valid contiguous
# mapping. A boundary-crossing or invalid span retains original per-read behavior.
a=s.index('Ge::DecodedVertex Ge::decode_vertex(');b=s.index('\n// ---------------------------------------------------------------- transform and lighting',a)
f=s[a:b]
anchor='    auto s8 = [&](std::uint32_t a)'
if f.count(anchor)!=1:raise SystemExit('Decoder diverso: nessun file modificato.')
f=f.replace('rd8(', 'vertex_rd8(').replace('rd16(', 'vertex_rd16(').replace('rd32(', 'vertex_rd32(')
i=f.index(anchor)
f=f[:i]+'''    const auto *contiguous = host(address, l.size * morphs);
    const auto vertex_rd8 = [&](std::uint32_t at) -> std::uint8_t {
        return contiguous ? contiguous[at - address] : rd8(at);
    };
    const auto vertex_rd16 = [&](std::uint32_t at) -> std::uint16_t {
        if (!contiguous) return rd16(at);
        std::uint16_t value;
        std::memcpy(&value, contiguous + (at - address), sizeof(value));
        return value;
    };
    const auto vertex_rd32 = [&](std::uint32_t at) -> std::uint32_t {
        if (!contiguous) return rd32(at);
        std::uint32_t value;
        std::memcpy(&value, contiguous + (at - address), sizeof(value));
        return value;
    };
'''+f[i:]
s=s[:a]+f+s[b:]
# Skip only normal computations whose result is unused without lighting.
s=change(s,'''    // Skinning.
    float model[3]''','''    // Skinning. Texture generation below uses the original input normal.
    const bool lighting = (regs_[kLightingEnable] & 1u) != 0;
    float model[3]''')
s=change(s,'            mul43(bones_[i], in.normal, tn, 0.0f);','            if (lighting) mul43(bones_[i], in.normal, tn, 0.0f);')
s=change(s,'                nn[c] += tn[c] * in.weights[i];','                if (lighting) nn[c] += tn[c] * in.weights[i];')
s=change(s,'    mul43(world_, normal, wn, 0.0f);','    if (lighting) mul43(world_, normal, wn, 0.0f);')
s=change(s,'    if ((regs_[kLightingEnable] & 1u) != 0) {\n        light(world, wn, out, in);','    if (lighting) {\n        light(world, wn, out, in);')
s=change(s,'''    const bool timed = full_timing || (backend_ != nullptr && (stats_.primitives & 63u) == 0);''','''    // Deterministic pseudo-random sampling avoids locking onto every 64th
    // command in repeating display lists. Weighted times remain estimates.
    vertex_timer_rng_ ^= vertex_timer_rng_ << 13;
    vertex_timer_rng_ ^= vertex_timer_rng_ >> 17;
    vertex_timer_rng_ ^= vertex_timer_rng_ << 5;
    const bool timed = full_timing || (backend_ != nullptr && (vertex_timer_rng_ & 63u) == 0);''')
s=change(s,'[ge] vertex reuse v6 active; vertex timers %s','[ge] vertex pipeline v7 active; vertex timers %s')
s=change(s,'''    std::uint32_t max_index = 0;
    for (unsigned i = 0; i < count; ++i) {''','''    if (index_format != 0) {
        ++vertex_indexed_draws_;
        if (++vertex_reuse_epoch_ == 0) {
            for (auto &entry : vertex_reuse_) entry.epoch = 0;
            vertex_reuse_epoch_ = 1;
        }
    }
    unsigned reused_this_draw = 0;
    for (unsigned i = 0; i < count; ++i) {''')
s=change(s,'''        max_index = std::max(max_index, index);
        verts.push_back(transform(decode_vertex(vaddr + index * vsize)));
        verts.back().source = vaddr + index * vsize;''','''        const auto address = vaddr + index * vsize;
        if (index_format != 0) {
            auto &entry = vertex_reuse_[index & 255u];
            if (entry.epoch == vertex_reuse_epoch_ && entry.index == index) {
                verts.push_back(entry.vertex);
                ++reused_this_draw;
                continue;
            }
            entry.vertex = transform(decode_vertex(address));
            entry.vertex.source = address;
            entry.index = index;
            entry.epoch = vertex_reuse_epoch_;
            verts.push_back(entry.vertex);
        } else {
            verts.push_back(transform(decode_vertex(address)));
            verts.back().source = address;
        }
        // Counters are accumulated once below, outside the hot vertex loop.''')
s=change(s,'''    const auto t_vertices = timed ? std::chrono::steady_clock::now() : t_start;''','''    vertex_reused_ += reused_this_draw;
    vertex_decoded_ += count - reused_this_draw;
    const auto t_vertices = timed ? std::chrono::steady_clock::now() : t_start;''')
for p in (hp,sp):
    if p.with_name(p.name+'.before-pipeline-v7').exists():raise SystemExit('Backup già presente: nessun file modificato.')
for p in (hp,sp):shutil.copy2(p,p.with_name(p.name+'.before-pipeline-v7'))
hp.write_text(h);sp.write_text(s)
print('V7 applicata: lettura vertici diretta, riuso indici per disegno, normali solo con illuminazione e contatori GE.')
