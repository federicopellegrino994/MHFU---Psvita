#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
r=Path.cwd().resolve();b=r/'build/pocket-pmo-live-v1-backup';m=b/'manifest.json'
sha=lambda x:hashlib.sha256(x).hexdigest()
if not m.is_file():raise SystemExit('Backup PMO live non trovato. Esegui da ~/mhfu-vita.')
updates=[]
for row in json.loads(m.read_text())['files']:
    p=(r/row['file']).resolve();old=(b/row['file']).resolve()
    if not p.is_relative_to(r) or not old.is_relative_to(b):raise SystemExit('Percorso non valido: nessuna modifica.')
    now=p.read_bytes() if p.is_file() else None
    if (sha(now) if now is not None else None)!=row['after']:raise SystemExit('Sorgente modificato dopo PMO live: nessun ripristino. '+row['file'])
    data=None
    if row['before'] is not None:
        if not old.is_file() or sha(old.read_bytes())!=row['before']:raise SystemExit('Backup modificato/mancante: nessuna modifica.')
        data=old.read_bytes()
    updates.append((p,data,now))
def write(p,data):
    if data is None:p.unlink(missing_ok=True)
    else:
        temp=p.with_name(p.name+'.pmo-live.tmp');temp.write_bytes(data);temp.replace(p)
try:
    for p,data,now in updates:write(p,data)
except Exception:
    for p,data,now in updates:write(p,now)
    raise
m.rename(b/'restored-manifest.json')
print('Sorgenti precedenti ripristinati. Ricompila e reinstalla il VPK.')
