#!/usr/bin/env python3
from pathlib import Path
import shutil
p=Path('vita/runtime/vita_gpu.cpp')
s=p.read_text()
if '[gpu-cost]' in s:
    raise SystemExit('Profilazione già applicata: nessun file modificato.')
for marker in ['class VitaGpu final', 'native rasterization test v1',
               'vglInitExtended(1024 * 1024, 960, 544', 'glVertex2f']:
    if marker not in s:
        raise SystemExit('Versione sorgente inattesa: nessun file modificato. Invia vita_gpu.cpp.')
new=Path(__file__).resolve().with_name('vita_gpu.cpp').read_text()
backup=p.with_name(p.name+'.before-profile-v2')
if backup.exists():
    raise SystemExit('Backup già esistente: nessun file modificato.')
shutil.copy2(p,backup)
p.write_text(new)
print('Profilazione GPU v2 e combinatore RGB/RGBA applicati. Timer e salvataggi invariati.')
