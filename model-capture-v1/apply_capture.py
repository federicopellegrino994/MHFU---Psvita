#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,shutil

r=Path.cwd();pkg=Path(__file__).resolve().parent
g=r/'platforms/psp/gpu/ge.cpp';p=r/'user_data/generated/ULES01213/pocket_model_probe.hpp'
pm=p.with_name('pocket_model_probe_manifest.json');h=g.with_name('pocket_model_capture.hpp');m=g.with_name('pocket_model_capture_manifest.json')
if not all(x.is_file() for x in (g,p,pm)):raise SystemExit('Esegui da ~/mhfu-vita con model-entry-probe-v1 ancora applicata. Nessuna modifica.')
if h.exists() or m.exists():raise SystemExit('Cattura già installata: nessuna modifica.')
old_manifest=json.loads(pm.read_text())
sha=lambda b:hashlib.sha256(b).hexdigest()
if sha(p.read_bytes())!=old_manifest['header_sha256']:raise SystemExit('Header della traccia modificato: nessuna modifica.')
for row in old_manifest['files']:
    f=p.parent/row['file']
    if not f.is_file() or sha(f.read_bytes())!=row['after']:raise SystemExit('Sorgenti generati modificati dopo la traccia: nessuna modifica.')
gs=g.read_text();ps=p.read_text()
if 'POCKET_U8_GPU_V12' not in gs:raise SystemExit('Richiesta base v12 verificata: nessuna modifica.')
def change(s,a,z):
    if s.count(a)!=1:raise SystemExit('Sorgenti diversi: nessuna modifica. Invia ge.cpp e pocket_model_probe.hpp.')
    return s.replace(a,z,1)
ps=change(ps,'#pragma once','#pragma once\n#include "../../../platforms/psp/gpu/pocket_model_capture.hpp"')
ps=change(ps,'    if(id>=counts.size())return;','''    if(id>=counts.size())return;
    if(id==0)pocket_model_capture::model(mem,self,a1,a2,a3,ra);''')
gs=change(gs,'#include "ge.hpp"','#include "ge.hpp"\n#include "pocket_model_capture.hpp"')
gs=change(gs,'void Ge::execute(std::uint32_t word, std::uint32_t) {','void Ge::execute(std::uint32_t word, std::uint32_t command_pc) {')
gs=change(gs,'    case kPrim: regs_[cmd] = data; draw_primitive(data); return;','''    case kPrim:
        regs_[cmd] = data;
        if(pocket_model_capture::phase.load(std::memory_order_acquire)==2){
            const auto ti=texture_info();
            pocket_model_capture::primitive(command_pc,data,vertex_size(),regs_.data(),
                world_.m.data(),view_.m.data(),proj_.m.data(),texgen_.m.data(),
                bones_.data(),sizeof(bones_),clut_.data(),sizeof(clut_),ti,
                [this](unsigned a,unsigned n){return host(a,n);});
        }
        draw_primitive(data);return;''')
updates={g:gs,p:ps}
for f in updates:
    if f.with_name(f.name+'.before-model-capture-v1').exists():raise SystemExit('Backup cattura già presente: nessuna modifica.')
payload=(pkg/'pocket_model_capture.hpp').read_bytes()
records=[{'file':str(f.relative_to(r)),'before':sha(f.read_bytes()),'after':sha(s.encode())} for f,s in updates.items()]
for f in updates:shutil.copy2(f,f.with_name(f.name+'.before-model-capture-v1'))
for f,s in updates.items():f.write_text(s)
h.write_bytes(payload);m.write_text(json.dumps({'files':records,'header_sha256':sha(payload)},indent=2))
print('Cattura installata: primo PMO statico idoneo, fino a 32 disegni GE, massimo 12 MiB. Non è una patch FPS.')
