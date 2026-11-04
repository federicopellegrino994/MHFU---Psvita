#!/usr/bin/env python3
from pathlib import Path
import json,hashlib,shutil
r=Path.cwd();m=r/'platforms/psp/gpu/pocket_model_capture_manifest.json';h=m.with_name('pocket_model_capture.hpp')
if not m.is_file() or not h.is_file():raise SystemExit('Cattura non trovata. Esegui da ~/mhfu-vita.')
d=json.loads(m.read_text());sha=lambda b:hashlib.sha256(b).hexdigest()
if sha(h.read_bytes())!=d['header_sha256']:raise SystemExit('Header cattura modificato: nessun ripristino.')
for row in d['files']:
    p=r/row['file'];b=p.with_name(p.name+'.before-model-capture-v1')
    if not p.is_file() or not b.is_file() or sha(p.read_bytes())!=row['after'] or sha(b.read_bytes())!=row['before']:raise SystemExit('Sorgente o backup modificato/mancante: nessun ripristino.')
for row in d['files']:
    p=r/row['file'];shutil.copy2(p.with_name(p.name+'.before-model-capture-v1'),p)
h.unlink();m.unlink()
print('Cattura rimossa dai sorgenti. Ricompila e reinstalla il VPK. Ora puoi anche rimuovere la traccia model-entry-probe-v1.')
