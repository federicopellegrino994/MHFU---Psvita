#!/usr/bin/env python3
"""Read-only preflight. Does not download, install, or change build settings."""
import os
from pathlib import Path
import subprocess
import sys
sdk = Path(os.environ.get('VITASDK', str(Path.home() / 'vitasdk')))
files = ['include/vitaGL.h', 'include/vitashark.h']
libs = ['vitaGL', 'mathneon', 'vitashark', 'SceShaccCgExt', 'zip', 'z',
        'taihen_stub', 'SceShaccCg_stub', 'SceGxm_stub', 'SceAppMgr_stub',
        'SceAppUtil_stub', 'SceCommonDialog_stub', 'SceKernelDmacMgr_stub']
files += ['lib/lib' + lib + '.a' for lib in libs]
missing = []
for name in files:
    p = sdk / 'arm-vita-eabi' / name
    print(('OK       ' if p.is_file() else 'MANCANTE ') + name)
    if not p.is_file(): missing.append(name)
if missing:
    print('\nPrerequisiti GPU incompleti: non avviare ancora la compilazione GPU.')
    print('Invia questo output per installare soltanto le dipendenze mancanti.')
    sys.exit(1)
# A readable archive is necessary; file presence alone does not establish integrity.
for lib in libs:
    path = sdk / 'arm-vita-eabi/lib' / ('lib' + lib + '.a')
    result = subprocess.run([str(sdk / 'bin/arm-vita-eabi-ar'), 't', str(path)],
                            capture_output=True, text=True)
    if result.returncode or not result.stdout.strip():
        raise SystemExit('Archivio non leggibile o vuoto: ' + str(path))
print('\nPrerequisiti presenti e archivi leggibili. ABI e collegamento restano da verificare nella build.')
print('Sulla Vita serve anche libshacccg.suprx (non incluso nel pacchetto).')
