#!/usr/bin/env python3
from pathlib import Path
import shutil,hashlib

r=Path.cwd();package=Path(__file__).resolve().parent
p=r/'vita/runtime/vita_gpu.cpp';m=r/'vita/runtime/rigid_gpu_math.hpp';header=r/'vita/runtime/rigid_visibility_cache.hpp'
if not p.is_file() or not m.is_file():raise SystemExit('Esegui da ~/mhfu-vita. Nessuna modifica.')
s=p.read_text()
if header.exists() or 'rigid_visibility_cache.hpp' in s:raise SystemExit('Cache v11 già presente: nessuna modifica.')
if '[render-v10] sparse profiling installed' not in s or '[gpu-rigid-cull] v9' not in s or 'POCKET_RIGID_CULL_V9' not in m.read_text():raise SystemExit('Richiesta la versione v10 su v9: nessuna modifica.')
if hashlib.sha256(m.read_bytes()).hexdigest()!='5b91dd2750952366d5622684f1672a9eca28c6e012a3bf034be135855d7bfdfa':raise SystemExit('Matematica del renderer differente da quella verificata: nessuna modifica. Invia rigid_gpu_math.hpp.')
def change(s,a,z):
    if s.count(a)!=1:raise SystemExit('Sorgente differente: nessuna modifica. Invia vita_gpu.cpp e rigid_gpu_math.hpp.')
    return s.replace(a,z,1)
s=change(s,'#include "rigid_gpu_math.hpp"','#include "rigid_gpu_math.hpp"\n#include "rigid_visibility_cache.hpp"')
s=change(s,'    pocket::rigid::Transform rigid_transform;','    pocket::rigid::Transform rigid_transform;\n    pocket::rigid::VisibilityCache visibility_cache;')
s=change(s,'            rigid_transform_valid=rigid_transform.build(p);rigid_generation=generation;','''            rigid_transform_valid=rigid_transform.build(p);rigid_generation=generation;
            if(rigid_transform_valid)visibility_cache.set_transform(rigid_transform);''')
s=change(s,'        rigid_scratch.resize(count);\n        bool all_safe=true,all_outside=true;','        bool all_safe=true,all_outside=true;')
s=change(s,'            const auto v=pocket::rigid::decode_111a(vertices+index_at(i)*12u,us,vs,uo,vo);','            const auto *position=vertices+index_at(i)*12u+6;')
s=change(s,'            const auto visibility=rigid_transform.classify(v.position[0],v.position[1],v.position[2]);','            const auto visibility=visibility_cache.classify(position);')
s=change(s,'''            auto &out=rigid_scratch[i];
            std::copy_n(v.position,4,out.position);std::copy_n(v.color,4,out.color);std::copy_n(v.uv,2,out.uv);
''','')
s=change(s,'        validate_measure.stop();','''        // Decode fresh colors and UVs only after eligibility is established.
        // Fully culled and fallback draws never allocate/copy scratch vertices.
        rigid_scratch.resize(count);
        for(unsigned i=0;i<count;++i){
            const auto v=pocket::rigid::decode_111a(vertices+index_at(i)*12u,us,vs,uo,vo);
            auto &out=rigid_scratch[i];
            std::copy_n(v.position,4,out.position);std::copy_n(v.color,4,out.color);std::copy_n(v.uv,2,out.uv);
        }
        validate_measure.stop();''')
s=change(s,'        rigid_scratch.reserve(256);','''        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-vertex-cache.flag","rb")){
            std::fclose(f);visibility_cache.enabled(false);
        }
        std::fprintf(stderr,"[gpu-vertex-cache] v11 %s; exact XYZ bytes + clip/bound/viewport; 4096 entries; colors/UV always fresh\\n",visibility_cache.enabled()?"enabled":"disabled by flag");
        rigid_scratch.reserve(256);''')
s=change(s,'            rigid_profile={};','''            std::fprintf(stderr,"[gpu-vertex-cache] period: hits=%u misses=%u\\n",
                static_cast<unsigned>(visibility_cache.hits()),static_cast<unsigned>(visibility_cache.misses()));
            visibility_cache.reset_counts();
            rigid_profile={};''')
backup=p.with_name(p.name+'.before-vertex-cache-v11')
if backup.exists():raise SystemExit('Backup v11 già presente: nessuna modifica.')
payload=(package/'rigid_visibility_cache.hpp').read_bytes()
shutil.copy2(p,backup);p.write_text(s);header.write_bytes(payload)
print('Cache vertici v11 applicata; backup vita_gpu.cpp.before-vertex-cache-v11 creato. FPS da verificare sulla Vita.')
