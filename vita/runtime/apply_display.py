from pathlib import Path
import shutil

root = Path.cwd()
ge = root / 'platforms/psp/runtime/src/hle_ge.cpp'
boot = root / 'vita/runtime/game_boot.cpp'
cmake = root / 'vita/runtime/boot_target.cmake'
s, b, c = ge.read_text(), boot.read_text(), cmake.read_text()
marker = '// Pocket software display hook'
if marker in s:
    raise SystemExit('Display patch already present. Do not apply twice.')

worker_old = '''                if (backend) {
                    lock.unlock();
                    backend->present(p.address, p.stride, p.format);
                    lock.lock();
                }'''
worker_new = worker_old + '''
#if defined(__vita__)
                else if (pocket_software_present) {
                    lock.unlock();
                    pocket_software_present(rt, p.address, p.stride, p.format);
                    lock.lock();
                }
#endif'''
guard_old = '    if (!s.backend_factory || s.synchronous) return;'
guard_new = '''#if defined(__vita__)
    if ((!s.backend_factory && !pocket_software_present) || s.synchronous) return;
#else
    if (!s.backend_factory || s.synchronous) return;
#endif'''
header = '''
// Pocket software display hook: optional, provided only by the diagnostic runner.
#if defined(__vita__)
extern "C" void pocket_software_present(mhf::psp::Runtime *, std::uint32_t,
                                        std::uint32_t, std::uint32_t) __attribute__((weak));
#endif
'''
checks = [(s, worker_old), (s, guard_old), (s, '#include <vector>'),
          (b, '#include "mhf/psp/runtime.hpp"'),
          (b, 'sceCtrlPeekBufferPositive(0, &pad, 1);')]
for text, needle in checks:
    if text.count(needle) != 1:
        raise SystemExit('Source differs from expected: ' + needle[:75] + '. No files changed.')
if 'pocket-game-boot' not in c or 'whole-archive' not in c:
    raise SystemExit('Expected boot target with pthread fix not found. No files changed.')

s = s.replace(worker_old, worker_new, 1).replace(guard_old, guard_new, 1)
s = s.replace('#include <vector>', '#include <vector>\n'+header, 1)
b = b.replace('#include "mhf/psp/runtime.hpp"', '#include "mhf/psp/runtime.hpp"\n#include "vita_present.hpp"', 1)
b = b.replace('sceCtrlPeekBufferPositive(0, &pad, 1);',
              'sceCtrlPeekBufferPositive(0, &pad, 1);\n            pocket_update_input(runtime, pad);',1)
c += '''
# Pocket software display hook
 target_sources(pocket-game-boot PRIVATE "${CMAKE_CURRENT_LIST_DIR}/vita_present.cpp")
 target_link_libraries(pocket-game-boot PRIVATE SceDisplay_stub)
'''
for path, text in ((ge,s),(boot,b),(cmake,c)):
    backup = path.with_name(path.name + '.before-display')
    if not backup.exists(): shutil.copy2(path, backup)
    path.write_text(text)
print('Software presentation and input installed. Existing timing/pthread fixes preserved.')
