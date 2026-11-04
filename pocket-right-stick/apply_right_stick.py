#!/usr/bin/env python3
from pathlib import Path
import shutil

p = Path('vita/runtime/vita_present.cpp')
if not p.is_file():
    raise SystemExit('Esegui dalla cartella ~/mhfu-vita: nessun file modificato.')
s = p.read_text()
if 'POCKET_RIGHT_STICK_DPAD_V1' in s:
    raise SystemExit('Stick destro già configurato: nessun file modificato.')
anchor = '    state.lx=pad.lx; state.ly=pad.ly;'
if s.count(anchor) != 1 or 'void pocket_update_input(' not in s or 'set_controller_state(rt,state)' not in s:
    raise SystemExit('Sorgente input diverso: nessun file modificato. Invia vita/runtime/vita_present.cpp.')
patch = '''    // POCKET_RIGHT_STICK_DPAD_V1: right stick adds PSP D-pad buttons.
    // Engage beyond 80/176; release at 104/152 to prevent threshold chatter.
    // One state per axis, so the stick itself never sends opposite directions.
    static int right_x = 0, right_y = 0;
    const auto axis = [](unsigned value, int previous) {
        if (value <= 80) return -1;
        if (value >= 176) return 1;
        if (previous < 0 && value < 104) return -1;
        if (previous > 0 && value > 152) return 1;
        return 0;
    };
    right_x = axis(pad.rx, right_x);
    right_y = axis(pad.ry, right_y);
    if (right_x < 0) state.buttons |= kLeft;
    if (right_x > 0) state.buttons |= kRight;
    if (right_y < 0) state.buttons |= kUp;
    if (right_y > 0) state.buttons |= kDown;
    static bool input_logged = false;
    if (!input_logged) {
        std::fprintf(stderr, "[input] right stick -> PSP D-pad; deadzone and hysteresis active\\n");
        input_logged = true;
    }
'''
backup = p.with_name(p.name + '.before-right-stick-v1')
if backup.exists():
    raise SystemExit('Backup già presente: nessun file modificato.')
updated = s.replace(anchor, patch + anchor, 1)
shutil.copy2(p, backup)
p.write_text(updated)
print('Stick destro configurato: su/giu/sinistra/destra equivalenti alla croce direzionale PSP.')
