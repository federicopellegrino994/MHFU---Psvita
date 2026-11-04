#!/usr/bin/env python3
from pathlib import Path
import re,hashlib,json,shutil

root=Path.cwd();package=Path(__file__).resolve().parent
gen=root/'user_data/generated/ULES01213';elf=root/'user_data/prepared/ULES01213/prepared.elf'
header=gen/'pocket_model_probe.hpp';manifest=gen/'pocket_model_probe_manifest.json'
targets=[('drawMesh',0x0886171C),('drawWeight',0x088646F0),('drawWeightMesh',0x0886477C),('compile',0x088623DC)]
if not gen.is_dir() or not elf.is_file():raise SystemExit('Esegui da ~/mhfu-vita; servono generated/ULES01213 e prepared/ULES01213/prepared.elf. Nessuna modifica.')
if hashlib.sha1(elf.read_bytes()).hexdigest()!='04b60ee797bd628faa0525ebd52186fa05967963':raise SystemExit('ELF diverso dalla versione EU verificata: nessuna modifica.')
if header.exists() or manifest.exists():raise SystemExit('Traccia già presente: nessuna modifica.')
patterns=[re.compile(r'void\s+f_'+f'{a:08X}'+r'\s*\(\s*Runtime\s*&\s*rt\s*,\s*Context\s*&\s*ctx\s*\)\s*\{',re.I) for _,a in targets]
found=[[] for _ in targets];sources={}
for p in sorted(gen.glob('*.cpp')):
    s=p.read_text()
    for i,pattern in enumerate(patterns):
        matches=list(pattern.finditer(s))
        for m in matches:found[i].append((p,m.end()))
        if matches:sources[p]=s
for i,hits in enumerate(found):
    if len(hits)!=1:raise SystemExit(f'{targets[i][0]}: trovate {len(hits)} definizioni, attesa 1. Nessuna modifica; invia questo messaggio.')
updates=dict(sources)
for p,s in sources.items():
    patches=[]
    for i,hits in enumerate(found):
        at,end=hits[0]
        if at==p:patches.append((end,f'\n    ::pocket_model_probe::enter({i}u, abi::memory(rt), ctx.gpr[4], ctx.gpr[5], ctx.gpr[6], ctx.gpr[7], ctx.gpr[31]); // POCKET_MODEL_ENTRY_V1\n'))
    for end,code in sorted(patches,reverse=True):s=s[:end]+code+s[end:]
    updates[p]='#include "pocket_model_probe.hpp"\n'+s
for p in updates:
    if p.with_name(p.name+'.before-model-entry-v1').exists():raise SystemExit('Backup già presente: nessuna modifica.')
payload=(package/'pocket_model_probe.hpp').read_bytes()
records=[]
for p,new in updates.items():
    old=p.read_bytes();records.append({'file':p.name,'before':hashlib.sha256(old).hexdigest(),'after':hashlib.sha256(new.encode()).hexdigest()})
for p in updates:shutil.copy2(p,p.with_name(p.name+'.before-model-entry-v1'))
header.write_bytes(payload)
for p,new in updates.items():p.write_text(new)
manifest.write_text(json.dumps({'files':records,'header_sha256':hashlib.sha256(payload).hexdigest()},indent=2))
print(f'Traccia applicata a 4 ingressi in {len(updates)} file generati. Osservazione soltanto: nessuna ottimizzazione FPS.')
for i,(name,address) in enumerate(targets):print(f'{name} {address:08X}: {found[i][0][0].name}')
