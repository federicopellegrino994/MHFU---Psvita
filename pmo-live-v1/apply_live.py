#!/usr/bin/env python3
from pathlib import Path
import hashlib,json,re

r=Path.cwd().resolve();pkg=Path(__file__).resolve().parent
gen=r/'user_data/generated/ULES01213';elf=r/'user_data/prepared/ULES01213/prepared.elf'
backup=r/'build/pocket-pmo-live-v1-backup'
sha=lambda b:hashlib.sha256(b).hexdigest()
def stop(s):raise SystemExit(s+' Nessuna modifica.')
if not gen.is_dir() or not elf.is_file():stop('Esegui da ~/mhfu-vita con la versione EU preparata.')
if hashlib.sha1(elf.read_bytes()).hexdigest()!='04b60ee797bd628faa0525ebd52186fa05967963':stop('ELF diverso dalla versione EU verificata.')
if backup.exists():stop('Backup PMO live già presente.')
if (gen/'pocket_model_probe_manifest.json').exists() or (r/'platforms/psp/gpu/pocket_model_capture_manifest.json').exists():
    stop('Diagnostiche model-entry/capture ancora presenti: invia questo messaggio per il ripristino corretto.')
d=json.loads((pkg/'payload.json').read_text());updates={};original={}
def plan(p,data):
    original[p]=p.read_bytes() if p.is_file() else None;updates[p]=data
for rel,record in d['files'].items():
    p=r/rel;now=p.read_bytes() if p.is_file() else None
    if (sha(now) if now is not None else None) not in record['before']:stop('Sorgente diverso dalla base verificata: '+rel+'. Invia questo file.')
    payload=(pkg/'payload'/rel).read_bytes()
    if sha(payload)!=record['after']:stop('Pacchetto incompleto o modificato: '+rel+'.')
    plan(p,payload)
# Remove only the byte-identical, now-unused v13 header. Its historical backups
# and the user's runtime flags remain available for a complete rollback.
resident=r/'vita/runtime/resident_geometry.hpp'
if resident.exists():
    if sha(resident.read_bytes())!=d['resident_header_sha256']:stop('Header v13 modificato.')
    plan(resident,None)
pattern=re.compile(r'void\s+f_0886171C\s*\(\s*Runtime\s*&\s*rt\s*,\s*Context\s*&\s*ctx\s*\)\s*\{',re.I)
hits=[]
for p in sorted(gen.glob('*.cpp')):
    s=p.read_text()
    if 'POCKET_PMO_LIVE_V1' in s:stop('PMO live già presente nei sorgenti generati.')
    for m in pattern.finditer(s):hits.append((p,s,m.end()))
if len(hits)!=1:stop(f'Trovate {len(hits)} definizioni drawMesh, attesa una.')
p,s,end=hits[0]
code='\n    if (::pocket::pmo::produce(abi::memory(rt), ctx.gpr[4], ctx.gpr[6], ctx.gpr[7])) return; // POCKET_PMO_LIVE_V1\n'
s=s[:end]+code+s[end:]
s='#include "../../../platforms/psp/gpu/pocket_pmo_bridge.hpp"\n'+s
plan(p,s.encode())
records=[{'file':str(p.relative_to(r)),'before':sha(original[p]) if original[p] is not None else None,'after':sha(data) if data is not None else None} for p,data in updates.items()]
backup.mkdir(parents=True)
for p,data in original.items():
    if data is not None:
        b=backup/p.relative_to(r);b.parent.mkdir(parents=True,exist_ok=True);b.write_bytes(data)
(backup/'manifest.json').write_text(json.dumps({'files':records},indent=2))
def write(p,data):
    if data is None:p.unlink(missing_ok=True)
    else:
        tmp=p.with_name(p.name+'.pmo-live.tmp');tmp.write_bytes(data);tmp.replace(p)
try:
    for p,data in updates.items():write(p,data)
except Exception:
    for p,data in original.items():write(p,data)
    raise
print('PMO live v1 collegato: produttore EU drawMesh -> gruppi GE -> backend Vita.')
print('V13 rimossa dal renderer. Backup completi verificati. Ricompila e reinstalla il VPK.')
print('Compilazione ARM e FPS sulla console ancora da verificare.')
