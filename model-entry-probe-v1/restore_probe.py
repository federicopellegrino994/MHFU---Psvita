#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,shutil
r=Path.cwd()/'user_data/generated/ULES01213';m=r/'pocket_model_probe_manifest.json';h=r/'pocket_model_probe.hpp'
if not m.is_file() or not h.is_file():raise SystemExit('Traccia completa non trovata. Esegui da ~/mhfu-vita.')
d=json.loads(m.read_text())
if hashlib.sha256(h.read_bytes()).hexdigest()!=d['header_sha256']:raise SystemExit('Header modificato dopo la traccia: ripristino interrotto.')
for row in d['files']:
    p=r/row['file'];backup=p.with_name(p.name+'.before-model-entry-v1')
    if not p.is_file() or not backup.is_file():raise SystemExit('Sorgente o backup mancante: nessuna modifica.')
    if hashlib.sha256(p.read_bytes()).hexdigest()!=row['after'] or hashlib.sha256(backup.read_bytes()).hexdigest()!=row['before']:raise SystemExit('File modificato dopo la traccia: nessuna modifica, invia i sorgenti.')
for row in d['files']:
    p=r/row['file'];shutil.copy2(p.with_name(p.name+'.before-model-entry-v1'),p)
h.unlink();m.unlink()
print('Sorgenti generati ripristinati. Ricompila e reinstalla il VPK per togliere la traccia dall’app.')
