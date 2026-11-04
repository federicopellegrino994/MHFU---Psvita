#!/usr/bin/env python3
"""Apply only to the exact source snapshot supplied by Federico. No game assets."""
from pathlib import Path
import hashlib
import shutil
import sys

EXPECTED = {'vita/runtime/game_boot.cpp': '238583dc2c425981aebf43de9c4fbcd951b330f599b0ea5fc690d8ce7392c850', 'vita/runtime/boot_target.cmake': '054e282bf90788b45c4770546d24a9caa2e757a30e663319f6918c254473b1f7'}
root = Path.cwd()
addon = Path(__file__).resolve().parent
changes = {}
for name, digest in EXPECTED.items():
    p = root / name
    if not p.is_file() or hashlib.sha256(p.read_bytes()).hexdigest() != digest:
        raise SystemExit("Sorgente diverso da quello ricevuto: " + name + ". Nessun file modificato.")
    changes[name] = p.read_text()

boot = changes["vita/runtime/game_boot.cpp"]
boot = boot.replace('#include "vita_present.hpp"', '#include "vita_present.hpp"\n#ifdef POCKET_VITA_GPU\n#include "vita_gpu.hpp"\n#endif', 1)
anchor = '        mhf::psp::Runtime runtime(options);'
if boot.count(anchor) != 1:
    raise SystemExit("Punto di inizializzazione assente: nessun file modificato.")
boot = boot.replace(anchor, anchor + '\n#ifdef POCKET_VITA_GPU\n        pocket_install_gpu(runtime);\n#endif', 1)
boot = boot.replace('experimental game entry; no GPU/audio frontend; offline', 'experimental game entry; optional GPU; audio output unavailable; offline')
# Correct diagnostic text only: numeric timer was already 3630 seconds in supplied source.
boot = boot.replace('63630 seconds reached', '3630 seconds reached')
boot = boot.replace(r'\\n', r'\n')
changes['vita/runtime/game_boot.cpp'] = boot
changes['vita/runtime/boot_target.cmake'] += '\ninclude("${CMAKE_CURRENT_LIST_DIR}/gpu_target.cmake")\n'
new_files = ['vita/runtime/vita_gpu.cpp', 'vita/runtime/vita_gpu.hpp', 'vita/runtime/gpu_target.cmake']
for name in new_files:
    if (root / name).exists():
        raise SystemExit("File GPU già presente: " + name + ". Nessun file modificato.")
    if not (addon / name).is_file():
        raise SystemExit("Pacchetto incompleto: " + name)
for name in changes:
    backup = root / (name + '.before-gpu-v1')
    if backup.exists():
        raise SystemExit("Backup GPU già presente: nessun file modificato.")
# Validate every input before making any changes.
for name in changes:
    shutil.copy2(root / name, root / (name + '.before-gpu-v1'))
for name in new_files:
    shutil.copy2(addon / name, root / name)
for name, content in changes.items():
    (root / name).write_text(content)
print("Addon GPU applicato. Il limite del test resta di un'ora.")
print("Prima esegui check_gpu_deps.py. GPU disattivata finché POCKET_VITA_GPU non viene impostato a ON.")
