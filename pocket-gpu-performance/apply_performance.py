#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path.cwd()
addon=Path(__file__).resolve().parent
p=root/'vita/runtime/vita_gpu.cpp'
g=root/'platforms/psp/gpu/ge.cpp'
s=p.read_text(); gs=g.read_text()
if 'v3-batch-xxh3' in s:
    raise SystemExit('Ottimizzazione già presente: nessuna modifica.')
for marker in ['native rasterization test v2-profile','[gpu-cost]','profile.requests','texture_combiner']:
    if marker not in s:
        raise SystemExit('Richiesta GPU v2: sorgente diverso, nessun file modificato.')
if 'POCKET_GE_SHADER_STACK' not in (root/'platforms/psp/runtime/src/hle_ge.cpp').read_text():
    raise SystemExit('Applicare prima la correzione stack del thread grafico.')
if not (root/'third_party/xxhash/xxhash.h').is_file():
    raise SystemExit('Header xxHash del progetto assente: nessun file modificato.')
start=gs.find('std::uint64_t Ge::texture_hash() const {')
end=gs.find('\n}\n',start)
if start<0 or end<0 or '0xCBF29CE484222325' not in gs[start:end]:
    raise SystemExit('Hash di partenza diverso dal previsto: nessun file modificato.')
if '#include "ge.hpp"' not in gs:
    raise SystemExit('Include GE non trovato: nessun file modificato.')
new_hash='''// POCKET_XXH3_TEXTURE_HASH: full-content validation without the serial FNV multiply chain.
std::uint64_t Ge::texture_hash() const {
    const TextureInfo t = texture_info();
    const std::array<std::uint32_t, 8> meta = {
        t.address, t.stride, t.format, t.width, t.height, t.swizzled ? 1u : 0u,
        t.bytes, t.format >= 4u && t.format <= 7u ? regs_[kClutFormat] : 0u
    };
    std::uint64_t hash = XXH3_64bits(meta.data(), sizeof(meta));
    if (const std::uint8_t *bytes = host(t.address, t.bytes))
        hash = XXH3_64bits_withSeed(bytes, t.bytes, hash);
    if (t.format >= 4u && t.format <= 7u)
        hash = XXH3_64bits_withSeed(clut_.data(), sizeof(clut_), hash);
    return hash;
}'''
gs=gs[:start]+new_hash+gs[end+2:]
gs=gs.replace('#include "ge.hpp"', '#include "ge.hpp"\n#define XXH_INLINE_ALL\n#include "../../../third_party/xxhash/xxhash.h"',1)
replacement=(addon/'vita_gpu.cpp').read_text()
for f in (p,g):
    if f.with_name(f.name+'.before-performance-v3').exists():
        raise SystemExit('Backup già presente: nessun file modificato.')
for f in (p,g): shutil.copy2(f,f.with_name(f.name+'.before-performance-v3'))
g.write_text(gs)
p.write_text(replacement)
print('V3 applicata: XXH3 e batching per stato identico. Timer, input e stack invariati.')
