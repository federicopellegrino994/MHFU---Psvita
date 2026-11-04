#!/usr/bin/env python3
from pathlib import Path
import shutil

r=Path.cwd();g=r/'platforms/psp/gpu/ge.cpp';b=r/'vita/runtime/vita_gpu.cpp'
if not g.is_file() or not b.is_file():raise SystemExit('Esegui da ~/mhfu-vita. Nessuna modifica.')
gs=g.read_text();bs=b.read_text()
if 'POCKET_U8_GPU_V12' in gs or '[gpu-u8] v12' in bs:raise SystemExit('V12 già presente: nessuna modifica.')
if '[gpu-vertex-cache] v11' not in bs:raise SystemExit('Richiesta v11: nessuna modifica.')
def change(s,a,z):
 if s.count(a)!=1:raise SystemExit('Sorgenti diversi: nessuna modifica. Invia ge.cpp e vita_gpu.cpp.')
 return s.replace(a,z,1)
gs=change(gs,'''    if (backend_ != nullptr && vt == 0x00111Au &&
        backend_->draw_rigid(*this, type, count, vaddr, iaddr)) {
        regs_[kIaddr] = iaddr + count * 2u;''','''    // POCKET_U8_GPU_V12: identical 12-byte vertex layout; only index width differs.
    if (backend_ != nullptr && (vt == 0x00111Au || vt == 0x00091Au) &&
        backend_->draw_rigid(*this, type, count, vaddr, iaddr)) {
        regs_[kIaddr] = iaddr + count * (index_format == 1 ? 1u : 2u);''')
bs=change(bs,'    bool rigid_transform_valid = false, rigid_enabled = true, rigid_cull = true;','''    bool rigid_transform_valid = false, rigid_enabled = true, rigid_cull = true;
    bool u8_enabled = true;
    struct U8Profile {unsigned attempts{}, draws{}, culled{};} u8_profile;''')
bs=change(bs,'        if (ge.reg(0x12) != 0x00111Au || type < 3 || type > 5 || count < 3 || count > 4096 ||','''        const auto vt=ge.reg(0x12);
        const bool u8=vt==0x00091Au;
        if(u8)++u8_profile.attempts;
        if ((vt != 0x00111Au && !(u8 && u8_enabled)) || type < 3 || type > 5 || count < 3 || count > 4096 ||''')
bs=change(bs,'''        const auto *indices=ge.guest_bytes(iaddr,count*2u);
        if (!indices) {++rigid_profile.memory;return false;}
        const auto index_at=[&](unsigned n){std::uint16_t v;std::memcpy(&v,indices+n*2,2);return v;};''','''        const unsigned index_bytes=u8?1u:2u;
        const auto *indices=ge.guest_bytes(iaddr,count*index_bytes);
        if (!indices) {++rigid_profile.memory;return false;}
        const auto index_at=[&](unsigned n)->unsigned{
            if(u8)return indices[n];
            std::uint16_t v;std::memcpy(&v,indices+n*2,2);return v;
        };''')
bs=change(bs,'                ++rigid_profile.culled_draws;rigid_profile.culled_vertices+=count;','                if(u8)++u8_profile.culled;\n                ++rigid_profile.culled_draws;rigid_profile.culled_vertices+=count;')
bs=change(bs,'        ++rigid_profile.draws;rigid_profile.vertices+=count;','        if(u8)++u8_profile.draws;\n        ++rigid_profile.draws;rigid_profile.vertices+=count;')
bs=change(bs,'        rigid_scratch.reserve(256);','''        if(FILE *f=std::fopen("ux0:data/pocketprobe/no-u8-gpu.flag","rb")){
            std::fclose(f);u8_enabled=false;
        }
        std::fprintf(stderr,"[gpu-u8] v12 %s; VTYPE 00091A; byte indices; same XYZ/color/UV layout as 00111A\\n",u8_enabled?"enabled":"disabled by flag");
        rigid_scratch.reserve(256);''')
bs=change(bs,'[gpu-rigid] v9 %s; VTYPE 00111A;','[gpu-rigid] v12 %s; VTYPE 00111A + optional 00091A;')
bs=change(bs,'            rigid_profile={};','''            std::fprintf(stderr,"[gpu-u8] period: attempts=%u draws=%u culled=%u fallback=%u\\n",
                u8_profile.attempts,u8_profile.draws,u8_profile.culled,
                u8_profile.attempts-u8_profile.draws-u8_profile.culled);
            u8_profile={};
            rigid_profile={};''')
for p in (g,b):
 if p.with_name(p.name+'.before-u8-v12').exists():raise SystemExit('Backup v12 già presente: nessuna modifica.')
for p in (g,b):shutil.copy2(p,p.with_name(p.name+'.before-u8-v12'))
g.write_text(gs);b.write_text(bs)
print('V12 applicata: formato 00091A con indici a 8 bit. Prestazioni da verificare su Vita.')
