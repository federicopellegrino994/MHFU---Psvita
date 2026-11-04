#!/usr/bin/env python3
"""Install the checked v12 -> resident-packet patch, removing known probes."""
from pathlib import Path
import hashlib,json,shutil

root=Path.cwd().resolve();pkg=Path(__file__).resolve().parent
backup=root/'build/pocket-resident-v13-backup'
gpu=root/'vita/runtime/vita_gpu.cpp';header=gpu.with_name('resident_geometry.hpp')
sha=lambda b:hashlib.sha256(b).hexdigest()
def stop(s):raise SystemExit(s+' Nessuna modifica.')
if not gpu.is_file():stop('Esegui da ~/mhfu-vita.')
if backup.exists() or header.exists():stop('V13 o suo backup già presente.')
expected=json.loads((pkg/'payload.json').read_text())
if sha(gpu.read_bytes())!=expected['base_gpu_sha256']:stop('vita_gpu.cpp diverso dalla base v12 verificata. Invia questo file.')
for name in ('vita_gpu.cpp','resident_geometry.hpp'):
    if sha((pkg/name).read_bytes())!=expected[name]:stop('Pacchetto incompleto o modificato: '+name+'.')
original={};updates={}
def path(s):
    p=(root/s).resolve()
    if not p.is_relative_to(root):stop('Percorso di backup non valido.')
    return p
def current(p):return updates[p] if p in updates else p.read_bytes() if p.is_file() else None
def plan(p,data):
    if p not in original:original[p]=p.read_bytes() if p.is_file() else None
    updates[p]=data
def remove_probe(manifest,header_path,prefix,suffix):
    if not manifest.exists():
        if current(header_path) is not None:stop('Header diagnostico senza manifesto: '+str(header_path.relative_to(root))+'.')
        return
    d=json.loads(manifest.read_text())
    h=current(header_path)
    if h is None or sha(h)!=d['header_sha256']:stop('Header diagnostico modificato dopo il test.')
    for row in d['files']:
        p=path(str(prefix/row['file']));old=p.with_name(p.name+suffix)
        now=current(p)
        if now is None or not old.is_file() or sha(now)!=row['after'] or sha(old.read_bytes())!=row['before']:
            stop('Sorgente/backup diagnostico modificato: '+str(p.relative_to(root))+'.')
        plan(p,old.read_bytes())
    plan(header_path,None);plan(manifest,None)

# Preflight both dependent restorations against a virtual view of the files.
# No file is written until every source and backup has been validated.
remove_probe(root/'platforms/psp/gpu/pocket_model_capture_manifest.json',
             root/'platforms/psp/gpu/pocket_model_capture.hpp',Path('.'),'.before-model-capture-v1')
gen=Path('user_data/generated/ULES01213')
remove_probe(root/gen/'pocket_model_probe_manifest.json',root/gen/'pocket_model_probe.hpp',gen,'.before-model-entry-v1')
plan(gpu,(pkg/'vita_gpu.cpp').read_bytes());plan(header,(pkg/'resident_geometry.hpp').read_bytes())
rows=[]
for p,data in updates.items():
    old=original[p];rows.append({'file':str(p.relative_to(root)),'before':sha(old) if old is not None else None,'after':sha(data) if data is not None else None})
backup.mkdir(parents=True)
for p,data in original.items():
    if data is not None:
        b=backup/p.relative_to(root);b.parent.mkdir(parents=True,exist_ok=True);b.write_bytes(data)
(backup/'manifest.json').write_text(json.dumps({'files':rows},indent=2))
def write(p,data):
    if data is None:p.unlink(missing_ok=True)
    else:
        tmp=p.with_name(p.name+'.resident-v13.tmp');tmp.write_bytes(data);tmp.replace(p)
try:
    for p,data in updates.items():write(p,data)
except Exception:
    for p,data in original.items():write(p,data)
    raise
print('V13 applicata. Diagnostiche model-entry/capture rimosse se presenti; backup verificati.')
print('Ora compila e reinstalla il VPK. FPS e correttezza su Vita ancora da verificare.')
