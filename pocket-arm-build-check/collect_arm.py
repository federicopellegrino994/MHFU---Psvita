#!/usr/bin/env python3
"""Read-only collection of renderer ARM objects and their compiler options."""
from pathlib import Path
import argparse
import hashlib
import json
import struct
import zipfile

parser=argparse.ArgumentParser(description='Raccoglie gli oggetti ARM del renderer, senza compilare o modificare sorgenti.')
parser.add_argument('--root',type=Path,default=Path.cwd())
args=parser.parse_args();root=args.root.resolve();build=root/'build/full-runtime-vita'
if not build.is_dir():raise SystemExit('Cartella build/full-runtime-vita assente. Esegui lo script da ~/mhfu-vita.')
allowed={'ge.cpp.o','ge.cpp.obj','vita_gpu.cpp.o','vita_gpu.cpp.obj'}
objects=[];skipped=[]
for p in sorted(build.rglob('*')):
 if not p.is_file() or p.name not in allowed:continue
 with p.open('rb') as f:header=f.read(20)
 if len(header)<20 or header[:4]!=b'\x7fELF' or header[5]!=1 or struct.unpack_from('<H',header,18)[0]!=40:
  skipped.append(str(p.relative_to(root)));continue
 objects.append(p)
found={p.name.removesuffix('.obj').removesuffix('.o') for p in objects}
missing={'ge.cpp','vita_gpu.cpp'}-found
if missing:
 raise SystemExit('Oggetti ARM mancanti: '+', '.join(sorted(missing))+'. Invia questo messaggio; nessuna compilazione avviata.')
files=set(objects)
for p in objects:
 for parent in p.parents:
  if parent==build.parent:break
  if parent.name.endswith('.dir'):
   for name in ['flags.make','link.txt','compiler_depend.make']:
    # Dependency maps can be huge and contain no extra compiler options.
    if name=='compiler_depend.make':continue
    q=parent/name
    if q.is_file():files.add(q)
   break
for rel in ['CMakeFiles/pocket-game-boot.dir/flags.make','CMakeFiles/pocket-game-boot.dir/link.txt']:
 p=build/rel
 if p.is_file():files.add(p)
if sum(p.stat().st_size for p in files)>40*1024*1024:
 raise SystemExit('La raccolta supera 40 MiB. Invia questo messaggio; nessun archivio creato.')
records=[]
for p in sorted(files):
 data=p.read_bytes();st=p.stat()
 records.append({'path':str(p.relative_to(root)),'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest(),'mtime_ns':st.st_mtime_ns,'arm_object':p in objects})
cache=build/'CMakeCache.txt';options=[]
if cache.is_file():
 for line in cache.read_text(errors='replace').splitlines():
  if line.startswith(('CMAKE_BUILD_TYPE:','CMAKE_CXX_COMPILER:','CMAKE_CXX_FLAGS:','CMAKE_CXX_FLAGS_RELEASE:','CMAKE_CXX_FLAGS_RELWITHDEBINFO:','CMAKE_TOOLCHAIN_FILE:')):options.append(line)
binary=build/'pocket-game-boot'
manifest={'purpose':'Renderer ARM analysis; no rebuild, source edits, ISO or generated game objects.',
 'files':records,'skipped_non_arm':skipped,'cmake_options':options,
 'linked_binary_mtime_ns':binary.stat().st_mtime_ns if binary.is_file() else None}
output=root/'build/arm-render-diagnostics.zip';tmp=output.with_name(output.name+'.tmp')
with zipfile.ZipFile(tmp,'w',compression=zipfile.ZIP_STORED) as z:
 for p in sorted(files):z.write(p,str(p.relative_to(root)))
 z.writestr('manifest.json',json.dumps(manifest,indent=2,ensure_ascii=False))
with zipfile.ZipFile(tmp) as z:
 if z.testzip() is not None:raise SystemExit('Verifica archivio fallita; nessun archivio finale sostituito.')
tmp.replace(output)
print('Raccolta completata:',output)
print('Oggetti ARM:',len(objects),'| dimensione archivio:',round(output.stat().st_size/1024/1024,2),'MiB')
print('Allega arm-render-diagnostics.zip in chat. Non serve reinstallare il gioco.')
