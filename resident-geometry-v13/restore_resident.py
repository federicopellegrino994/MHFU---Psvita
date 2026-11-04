#!/usr/bin/env python3
from pathlib import Path
import hashlib,json
r=Path.cwd().resolve();backup=r/'build/pocket-resident-v13-backup';m=backup/'manifest.json'
sha=lambda b:hashlib.sha256(b).hexdigest()
if not m.is_file():raise SystemExit('Backup v13 non trovato. Esegui da ~/mhfu-vita.')
d=json.loads(m.read_text());pending=[]
for row in d['files']:
    p=(r/row['file']).resolve();b=(backup/row['file']).resolve()
    if not p.is_relative_to(r) or not b.is_relative_to(backup):raise SystemExit('Percorso non valido: nessuna modifica.')
    now=p.read_bytes() if p.is_file() else None
    if (sha(now) if now is not None else None)!=row['after']:raise SystemExit('Sorgente modificato dopo v13: nessun ripristino. '+row['file'])
    old=None
    if row['before'] is not None:
        if not b.is_file() or sha(b.read_bytes())!=row['before']:raise SystemExit('Backup modificato/mancante: nessun ripristino.')
        old=b.read_bytes()
    pending.append((p,old,now))
def write(p,data):
    if data is None:p.unlink(missing_ok=True)
    else:
        tmp=p.with_name(p.name+'.resident-v13.tmp');tmp.write_bytes(data);tmp.replace(p)
try:
    for p,old,now in pending:write(p,old)
except Exception:
    for p,old,now in pending:write(p,now)
    raise
m.rename(backup/'restored-manifest.json')
print('Sorgenti precedenti ripristinati, comprese eventuali diagnostiche presenti prima di v13.')
print('Ricompila e reinstalla il VPK per applicare il ripristino sulla Vita.')
