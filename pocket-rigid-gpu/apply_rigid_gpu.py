#!/usr/bin/env python3
from pathlib import Path
import shutil
r=Path.cwd();root=Path(__file__).resolve().parent
paths={k:r/v for k,v in {
 'h':'platforms/psp/gpu/ge.hpp','s':'platforms/psp/gpu/ge.cpp',
 'api':'platforms/psp/gpu/backend.hpp','probe':'platforms/psp/gpu/pocket_geometry_probe.hpp',
 'b':'vita/runtime/vita_gpu.cpp'}.items()}
if not all(p.is_file() for p in paths.values()):raise SystemExit('Richiesti v7, GPU e censimento v1. Esegui da ~/mhfu-vita: nessuna modifica.')
f={k:p.read_text() for k,p in paths.items()}
if 'POCKET_RIGID_GPU_V8' in f['h']:raise SystemExit('Percorso GPU v8 già applicato: nessuna modifica.')
if 'POCKET_VERTEX_PIPELINE_V7' not in f['h'] or 'POCKET_GE_CENSUS_V1' not in f['h'] or 'POCKET_STATE_FAST_V5' not in f['b']:raise SystemExit('Versioni sorgenti diverse: nessuna modifica.')
def change(text,old,new):
 if text.count(old)!=1:raise SystemExit('Sorgente diverso: nessun file modificato. Invia ge.cpp/ge.hpp/backend.hpp/vita_gpu.cpp.')
 return text.replace(old,new,1)
f['api']=change(f['api'],'    virtual ~Backend() = default;','''    virtual ~Backend() = default;
    // Return true only after consuming this entire draw. False keeps the
    // reference GE pipeline. Raw guest memory is copied before returning.
    virtual bool draw_rigid(const Ge &, unsigned, unsigned, std::uint32_t, std::uint32_t) { return false; }''')
f['h']=change(f['h'],'    [[nodiscard]] const Matrix43 &view_matrix() const noexcept { return view_; }','''    [[nodiscard]] const Matrix43 &view_matrix() const noexcept { return view_; }
    [[nodiscard]] const Matrix43 &world_matrix() const noexcept { return world_; }
    [[nodiscard]] std::uint64_t rigid_transform_generation() const noexcept { return rigid_transform_generation_; }''')
f['h']=change(f['h'],'    Backend *backend_{};','''    // POCKET_RIGID_GPU_V8: pre-transform backend entry.
    std::uint64_t rigid_transform_generation_{};
    Backend *backend_{};''')
f['s']=change(f['s'],'''    const std::uint32_t data = word & 0x00FFFFFFu;
    // PSP texture flush/sync''','''    const std::uint32_t data = word & 0x00FFFFFFu;
    if (cmd == kWorldMatrixData || cmd == kViewMatrixData || cmd == kProjMatrixData ||
        (cmd >= kViewportXScale && cmd <= kViewportZCenter) || cmd == kOffsetX || cmd == kOffsetY)
        ++rigid_transform_generation_;
    // PSP texture flush/sync''')
f['s']=change(f['s'],'    stats_.vertices += count;\n','''    stats_.vertices += count;
    // A handled indexed draw must advance the guest index pointer exactly as
    // the reference path does. Unsupported paths have no state side effects.
    if (backend_ != nullptr && vt == 0x00111Au &&
        backend_->draw_rigid(*this, type, count, vaddr, iaddr)) {
        regs_[kIaddr] = iaddr + count * 2u;
        return;
    }
''')
f['s']=f['s'].replace('[ge] census v1 installed; sampling 1/30 present intervals unless disabled','[ge] rigid GPU v8 entry installed; detailed census disabled')
f['probe']=change(f['probe'],'    bool enabled_=true, active_=true;','    bool enabled_=false, active_=false; // Detailed census disabled for performance testing.')
f['b']=change(f['b'],'#include "ge.hpp"','#include "ge.hpp"\n#include "rigid_gpu_math.hpp"')
f['b']=change(f['b'],'    std::vector<GVertex> batch;','''    std::vector<GVertex> batch;
    bool batch_model = false;
    pocket::rigid::Matrix batch_matrix{};
    std::vector<GVertex> rigid_scratch;
    pocket::rigid::Transform rigid_transform;
    std::uint64_t rigid_generation = ~std::uint64_t(0);
    bool rigid_transform_valid = false, rigid_enabled = true;
    struct RigidProfile { unsigned attempts{}, draws{}, vertices{}, state{}, memory{}, clip{}, matrix_splits{}, batches{}; } rigid_profile;''')
f['b']=change(f['b'],'    void draw_batch() {','''    // Keep CPU clip-space and GPU model-space batches separate. Matrix changes
    // must submit OLD vertices before changing the matrix attached to a batch.
    void begin_batch(bool model, const pocket::rigid::Matrix *matrix = nullptr) {
        if (!batch.empty() && (model != batch_model || (model && *matrix != batch_matrix))) {
            if (model && batch_model) ++rigid_profile.matrix_splits;
            draw_batch();
        }
        batch_model = model;
        if (model) batch_matrix = *matrix;
    }
    void draw_batch() {''')
f['b']=change(f['b'],'''        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.size()));
        batch.clear();''','''        if (batch_model) {
            glMatrixMode(GL_MODELVIEW); glLoadIdentity();
            glMatrixMode(GL_PROJECTION); glLoadMatrixf(batch_matrix.data());
            ++rigid_profile.batches;
        }
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(batch.size()));
        if (batch_model) {
            glMatrixMode(GL_PROJECTION); glLoadIdentity();
            glMatrixMode(GL_MODELVIEW);
        }
        batch.clear();''')
# Helper is inserted in the public interface, before draw_started.
anchor='    void draw_started(const gpu::DrawInfo &) override { prepared = false; ++serial; }'
raw='''    bool draw_rigid(const Ge &ge, unsigned type, unsigned count, unsigned vaddr, unsigned iaddr) override {
        if (!rigid_enabled) return false;
        ++rigid_profile.attempts;
        if (ge.reg(0x12) != 0x00111Au || type < 3 || type > 5 || count < 3 || count > 4096 ||
            (ge.reg(0x17)&1) || (ge.reg(0xd3)&1) || (ge.reg(0xc0)&3)) {
            ++rigid_profile.state; return false;
        }
        const auto generation = ge.rigid_transform_generation();
        if (!rigid_transform_valid || generation != rigid_generation) {
            pocket::rigid::Parameters p;
            p.world=ge.world_matrix().m; p.view=ge.view_matrix().m; p.projection=ge.proj_matrix().m;
            p.viewport={gpu::float24(ge.reg(0x42)),gpu::float24(ge.reg(0x43)),gpu::float24(ge.reg(0x44)),
                gpu::float24(ge.reg(0x45)),gpu::float24(ge.reg(0x46)),gpu::float24(ge.reg(0x47)),
                float(ge.reg(0x4c)&65535)/16,float(ge.reg(0x4d)&65535)/16};
            rigid_transform_valid=rigid_transform.build(p);rigid_generation=generation;
        }
        if (!rigid_transform_valid) {++rigid_profile.clip;return false;}
        const auto *indices=ge.guest_bytes(iaddr,count*2u);
        if (!indices) {++rigid_profile.memory;return false;}
        const auto index_at=[&](unsigned n){std::uint16_t v;std::memcpy(&v,indices+n*2,2);return v;};
        unsigned max_index=0;
        for(unsigned i=0;i<count;++i)max_index=std::max(max_index,unsigned(index_at(i)));
        const auto *vertices=ge.guest_bytes(vaddr,(max_index+1)*12u);
        if(!vertices){++rigid_profile.memory;return false;}
        const float us=gpu::float24(ge.reg(0x48)),vs=gpu::float24(ge.reg(0x49));
        const float uo=gpu::float24(ge.reg(0x4a)),vo=gpu::float24(ge.reg(0x4b));
        if(!std::isfinite(us)||!std::isfinite(vs)||!std::isfinite(uo)||!std::isfinite(vo)){++rigid_profile.state;return false;}
        rigid_scratch.resize(count);
        for(unsigned i=0;i<count;++i){
            const auto v=pocket::rigid::decode_111a(vertices+index_at(i)*12u,us,vs,uo,vo);
            if(!rigid_transform.accepts(v.position[0],v.position[1],v.position[2])){
                ++rigid_profile.clip;return false;
            }
            auto &out=rigid_scratch[i];
            std::copy_n(v.position,4,out.position);std::copy_n(v.color,4,out.color);std::copy_n(v.uv,2,out.uv);
        }
        // No GL or batch mutation occurred before ALL vertices were validated.
        prepared=false;++serial;prepare(ge);
        begin_batch(true,&rigid_transform.gpu);
        const auto append=[&](const GVertex &v,const GVertex &color){
            GVertex out=v;
            if(flat)std::copy_n(color.color,4,out.color);
            if(target_texture){out.uv[0]*=float(texture_width)/Side;out.uv[1]=1-out.uv[1]*(float(texture_height)/Side);}
            batch.push_back(out);
        };
        const auto tri=[&](unsigned a,unsigned b,unsigned c){
            append(rigid_scratch[a],rigid_scratch[c]);append(rigid_scratch[b],rigid_scratch[c]);append(rigid_scratch[c],rigid_scratch[c]);
            if(batch.size()>=12288)draw_batch();
        };
        if(type==3)for(unsigned i=0;i+2<count;i+=3)tri(i,i+1,i+2);
        if(type==4)for(unsigned i=0;i+2<count;++i){if(i&1)tri(i+1,i,i+2);else tri(i,i+1,i+2);}
        if(type==5)for(unsigned i=1;i+1<count;++i)tri(0,i,i+1);
        ++rigid_profile.draws;rigid_profile.vertices+=count;
        return true;
    }
'''
f['b']=change(f['b'],anchor,raw+anchor)
f['b']=change(f['b'],'''        prepare(ge);
        batch.push_back(vertex(ge, a, flat ? c : a));''','''        prepare(ge);
        begin_batch(false);
        batch.push_back(vertex(ge, a, flat ? c : a));''')
f['b']=change(f['b'],'''        vglWaitVblankStart(GL_FALSE);
        plain_state();''','''        if (FILE *f=std::fopen("ux0:data/pocketprobe/no-rigid-gpu.flag","rb")) {
            std::fclose(f);rigid_enabled=false;
        }
        rigid_scratch.reserve(256);
        std::fprintf(stderr,"[gpu-rigid] v8 %s; VTYPE 00111A; GPU matrix transform with conservative CPU clipping checks\\n",rigid_enabled?"enabled":"disabled by flag");
        vglWaitVblankStart(GL_FALSE);
        plain_state();''')
f['b']=change(f['b'],'''            profile = {};
            last_perf''','''            std::fprintf(stderr,"[gpu-rigid] period: attempts=%u draws=%u vertices=%u fallback_state=%u fallback_memory=%u fallback_clip=%u batches=%u matrix_splits=%u\\n",
                rigid_profile.attempts,rigid_profile.draws,rigid_profile.vertices,rigid_profile.state,rigid_profile.memory,rigid_profile.clip,rigid_profile.batches,rigid_profile.matrix_splits);
            rigid_profile={};
            profile = {};
            last_perf''')
wrapper='    explicit GuardedGpu(std::unique_ptr<VitaGpu> p) : gpu(std::move(p)) {}'
f['b']=change(f['b'],wrapper,wrapper+'''
    bool draw_rigid(const Ge &g,unsigned type,unsigned count,std::uint32_t va,std::uint32_t ia) override {
        bool handled=false;invoke([&]{handled=gpu->draw_rigid(g,type,count,va,ia);});return handled;
    }''')
extra=paths['b'].with_name('rigid_gpu_math.hpp');payload=root/extra.name
if extra.exists() or not payload.is_file():raise SystemExit('Header assente o già presente: nessun file modificato.')
for p in paths.values():
 if p.with_name(p.name+'.before-rigid-v8').exists():raise SystemExit('Backup già presente: nessun file modificato.')
for p in paths.values():shutil.copy2(p,p.with_name(p.name+'.before-rigid-v8'))
shutil.copy2(payload,extra)
for k,p in paths.items():p.write_text(f[k])
print('Percorso GPU rigido v8 applicato: VTYPE 00111A, fallback conservativo, censimento dettagliato disattivato.')
