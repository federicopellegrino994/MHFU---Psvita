from pathlib import Path
import shutil
root = Path.cwd()
cmake = root / 'vita/runtime/CMakeLists.txt'
s = cmake.read_text()
if 'pocket_game_generated' not in s or 'MHF_PSP_OFFLINE' not in s:
    raise SystemExit('Expected full offline runtime CMakeLists.txt not found; stop.')
line = 'include("${CMAKE_CURRENT_LIST_DIR}/boot_target.cmake")'
if line not in s:
    backup = cmake.with_suffix('.txt.before-boot')
    if not backup.exists(): shutil.copy2(cmake, backup)
    cmake.write_text(s + '\n' + line + '\n')
print('Boot target added. Existing generated files are retained.')
