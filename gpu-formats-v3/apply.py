from pathlib import Path
import hashlib,json,sys
root=Path.cwd()
package=Path(__file__).resolve().parent
backup=root/'build/gpu-formats-v3-backup'
records=json.loads((package/'manifest.json').read_text())
sha=lambda data:hashlib.sha256(data).hexdigest()
restore='--restore' in sys.argv
updates={}
original={}
for rel,record in records.items():
    target=root/rel
    data=target.read_bytes()
    expected=record['after'] if restore else record['before']
    if sha(data)!=expected:
        raise SystemExit('Versione sorgente diversa da quella prevista: '+rel)
    source=backup/rel if restore else package/'payload'/target.name
    replacement=source.read_bytes()
    if sha(replacement)!=(record['before'] if restore else record['after']):
        raise SystemExit('Integrità del pacchetto o backup non valida.')
    original[target]=data
    updates[target]=replacement
if not restore:
    backup.mkdir(parents=True,exist_ok=False)
    for target,data in original.items():
        saved=backup/target.relative_to(root)
        saved.parent.mkdir(parents=True,exist_ok=True)
        saved.write_bytes(data)
try:
    for target,data in updates.items():
        tmp=target.with_name(target.name+'.screen-v2.tmp')
        tmp.write_bytes(data)
        tmp.replace(target)
except Exception:
    for target,data in original.items():
        target.write_bytes(data)
    raise
print('Ripristino completato.' if restore else 'GPU formats v3 applicata.')
print('Ricompila e reinstalla il VPK.')
