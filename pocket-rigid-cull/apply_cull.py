#!/usr/bin/env python3
from pathlib import Path
import shutil,hashlib
r=Path.cwd();root=Path(__file__).resolve().parent
b=r/'vita/runtime/vita_gpu.cpp';m=r/'vita/runtime/rigid_gpu_math.hpp';h=r/'platforms/psp/gpu/ge.hpp'
if not all(p.is_file() for p in (b,m,h)):raise SystemExit('Esegui da ~/mhfu-vita con v8 applicata: nessuna modifica.')
s=b.read_text();old=m.read_text()
if 'POCKET_RIGID_CULL_V9' in old:raise SystemExit('V9 già applicata: nessuna modifica.')
if 'POCKET_RIGID_GPU_V8' not in h.read_text() or '[gpu-rigid] v8' not in s:raise SystemExit('Richiesta v8: nessuna modifica.')
if hashlib.sha256(m.read_bytes()).hexdigest()!='ca85b1c3b9aa40272f15ed847194736d04c48e36c44e58a88d537fcae3bed43c':raise SystemExit('Header v8 differente: nessuna modifica. Invia rigid_gpu_math.hpp.')
def change(text,a,z):
 if text.count(a)!=1:raise SystemExit('Sorgenti diversi: nessuna modifica. Invia vita_gpu.cpp e rigid_gpu_math.hpp.')
 return text.replace(a,z,1)
s=change(s,'bool rigid_transform_valid = false, rigid_enabled = true;','bool rigid_transform_valid = false, rigid_enabled = true, rigid_cull = true;')
s=change(s,'struct RigidProfile { unsigned attempts{}, draws{}, vertices{}, state{}, memory{}, clip{}, matrix_splits{}, batches{}; }','struct RigidProfile { unsigned attempts{}, draws{}, vertices{}, state{}, memory{}, clip{}, matrix_splits{}, batches{}, culled_draws{}, culled_vertices{}, outside_near{}, outside_guard{}, boundary{}; }')
s=change(s,'''        rigid_scratch.resize(count);
        for(unsigned i=0;i<count;++i){''','''        rigid_scratch.resize(count);
        bool all_safe=true,all_outside=true;
        for(unsigned i=0;i<count;++i){''')
s=change(s,'''            if(!rigid_transform.accepts(v.position[0],v.position[1],v.position[2])){
                ++rigid_profile.clip;return false;
            }''','''            using Visibility=pocket::rigid::Transform::Visibility;
            const auto visibility=rigid_transform.classify(v.position[0],v.position[1],v.position[2]);
            if(visibility==Visibility::Safe) all_outside=false;
            else {
                all_safe=false;
                if(visibility==Visibility::OutsideNear)++rigid_profile.outside_near;
                else if(visibility==Visibility::OutsideGuard)++rigid_profile.outside_guard;
                else {++rigid_profile.boundary;all_outside=false;}
                if(!rigid_cull){++rigid_profile.clip;return false;}
            }''')
s=change(s,'''        // No GL or batch mutation occurred before ALL vertices were validated.''','''        if(!all_safe){
            if(all_outside){
                // Every vertex is certainly outside the near plane or, while
                // near-inside, outside the PSP guard band. Thus every triangle
                // either has a guard-band rejection or has no near-inside
                // vertices. The reference Ge::draw_triangle emits nothing.
                ++rigid_profile.culled_draws;rigid_profile.culled_vertices+=count;
                return true;
            }
            ++rigid_profile.clip;return false;
        }
        // No GL or batch mutation occurred before ALL vertices were validated.''')
s=change(s,'        rigid_scratch.reserve(256);','''        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-rigid-cull.flag","rb")){
            std::fclose(f);rigid_cull=false;
        }
        std::fprintf(stderr,"[gpu-rigid-cull] v9 %s; whole-draw PSP near/guard rejection only\\n",rigid_cull?"enabled":"disabled by flag");
        rigid_scratch.reserve(256);''')
s=change(s,'[gpu-rigid] v8 %s;','[gpu-rigid] v9 %s;')
s=change(s,'            rigid_profile={};','''            std::fprintf(stderr,"[gpu-rigid-cull] period: culled_draws=%u culled_vertices=%u outside_near=%u outside_guard=%u boundary_vertices=%u\\n",
                rigid_profile.culled_draws,rigid_profile.culled_vertices,rigid_profile.outside_near,rigid_profile.outside_guard,rigid_profile.boundary);
            rigid_profile={};''')
if 'bool accepts(float x,float y,float z)const' not in old or 'pocket::rigid' not in old:raise SystemExit('Header differente: nessuna modifica.')
new=(root/'rigid_gpu_math.hpp').read_text()
for p in (b,m):
 if p.with_name(p.name+'.before-cull-v9').exists():raise SystemExit('Backup già presente: nessuna modifica.')
for p in (b,m):shutil.copy2(p,p.with_name(p.name+'.before-cull-v9'))
m.write_text(new);b.write_text(s)
print('V9 applicata: scarto anticipato dei disegni certamente esclusi dal GE PSP; clipping incerto resta sul percorso CPU.')
