#!/usr/bin/env python3
from pathlib import Path
import shutil
root=Path.cwd();addon=Path(__file__).resolve().parent
backend=root/'vita/runtime/vita_gpu.cpp';header=root/'platforms/psp/gpu/ge.hpp';source=root/'platforms/psp/gpu/ge.cpp'
b=backend.read_text();h=header.read_text();s=source.read_text()
if 'POCKET_STATE_FAST_V5' in b:
    raise SystemExit('V5 già applicata: nessun file modificato.')
if 'v4-cache-epochs' not in b or 'texture_cache_generation_' not in h:
    raise SystemExit('Richiesta la v4: nessun file modificato.')
# Keep this set identical to every register in VitaGpu::state_key().
regs=[0x12,0x1e,0x1f,0x21,0x22,0x23,0x24,0x27,0x48,0x49,0x4a,0x4b,0x50,
      0x9c,0x9d,0x9e,0x9f,0xa0,0xa8,0xb0,0xb1,0xb8,0xc0,0xc2,0xc3,
      0xc5,0xc6,0xc7,0xc9,0xca,0xcf,0xd2,0xd3,0xd4,0xd5,0xd6,0xd7,
      0xd8,0xd9,0xda,0xdb,0xdc,0xdd,0xde,0xdf,0xe0,0xe1,0xe6,0xe7,0xe8,0xe9]
getter='    [[nodiscard]] std::uint64_t texture_cache_generation() const noexcept { return texture_cache_generation_; }'
field='    std::uint64_t texture_cache_generation_{};'
anchor='''        regs_[cmd] = data;
        return;
    }
}'''
for text,a in [(h,getter),(h,field),(s,anchor)]:
    if text.count(a)!=1:
        raise SystemExit('Struttura GE inattesa: nessun file modificato. Invia ge.cpp/ge.hpp.')
h=h.replace(getter,getter+'\n    [[nodiscard]] std::uint64_t render_state_generation() const noexcept { return render_state_generation_; }',1)
h=h.replace(field,field+'\n    std::uint64_t render_state_generation_{};',1)
cases='\n'.join('            case 0x%02X:'%r for r in regs)
update='''        // POCKET_STATE_GENERATION_V5: only state consumed by the hardware backend.
        // Matrices/vertices are already transformed in the GE and do not require
        // changing the raster state. Texture cache events have a separate counter.
        if (regs_[cmd] != data) {
            switch (cmd) {
'''+cases+'''
                ++render_state_generation_; break;
            default: break;
            }
        }
'''
s=s.replace(anchor,update+anchor,1)
for p in (backend,header,source):
    if p.with_name(p.name+'.before-state-v5').exists():raise SystemExit('Backup già presente: nessun file modificato.')
new=(addon/'vita_gpu.cpp').read_text()
for p in (backend,header,source):shutil.copy2(p,p.with_name(p.name+'.before-state-v5'))
header.write_text(h);source.write_text(s);backend.write_text(new)
print('V5 applicata: riuso stato GE tramite contatori. Timer, cache, stack e input invariati.')
