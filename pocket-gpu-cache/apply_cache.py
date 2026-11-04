#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path.cwd();addon=Path(__file__).resolve().parent
backend=root/'vita/runtime/vita_gpu.cpp'
header=root/'platforms/psp/gpu/ge.hpp'
source=root/'platforms/psp/gpu/ge.cpp'
b=backend.read_text();h=header.read_text();s=source.read_text()
if 'POCKET_TEXTURE_VALIDATION_V4' in b:
    raise SystemExit('V4 già applicata: nessun file modificato.')
if 'v3-batch-xxh3' not in b or 'POCKET_XXH3_TEXTURE_HASH' not in s:
    raise SystemExit('Richiesta la v3: nessun file modificato.')
if 'POCKET_GE_SHADER_STACK' not in (root/'platforms/psp/runtime/src/hle_ge.cpp').read_text():
    raise SystemExit('Manca la correzione dello stack GE: nessun file modificato.')
getter='    [[nodiscard]] std::uint64_t clut_generation() const noexcept { return clut_generation_; }'
field='    std::uint64_t clut_generation_{};'
execute='''    const std::uint32_t data = word & 0x00FFFFFFu;
    switch (cmd) {'''
for text,anchor in [(h,getter),(h,field),(s,execute)]:
    if text.count(anchor)!=1:
        raise SystemExit('Struttura GE diversa: nessun file modificato. Invia ge.hpp e ge.cpp.')
h=h.replace(getter,getter+'\n    [[nodiscard]] std::uint64_t texture_cache_generation() const noexcept { return texture_cache_generation_; }',1)
h=h.replace(field,field+'\n    std::uint64_t texture_cache_generation_{};',1)
s=s.replace(execute,'''    const std::uint32_t data = word & 0x00FFFFFFu;
    // PSP texture flush/sync, palette load and GE image copy end validation epochs.
    // Increment on EVERY command, even if its argument equals the previous value.
    if (cmd == 0xCB || cmd == 0xCC || cmd == kLoadClut || cmd == kTransferStart)
        ++texture_cache_generation_;
    switch (cmd) {''',1)
for p in (backend,header,source):
    if p.with_name(p.name+'.before-cache-v4').exists():
        raise SystemExit('Backup già presente: nessun file modificato.')
new=(addon/'vita_gpu.cpp').read_text()
for p in (backend,header,source):shutil.copy2(p,p.with_name(p.name+'.before-cache-v4'))
header.write_text(h);source.write_text(s);backend.write_text(new)
print('V4 applicata: cache per eventi GE e misure campionate. Timer e stack invariati.')
