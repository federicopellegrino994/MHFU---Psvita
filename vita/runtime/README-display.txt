Pocket software display addon — experimental source only, GPL-3.0-or-later.

Unzip into the existing project root, then run:
python3 vita/runtime/apply_display.py
Reconfigure existing vita/runtime build and build pocket-game-boot.vpk-vpk.
The installer checks expected source anchors before modifying files and preserves
backups named *.before-display. It refuses duplicate application. Keep prior
pthread whole-archive linking, quiet trace and diagnostic audio pacing changes.

The optional Vita-only weak hook presents software-rendered guest VRAM in the
existing GE worker's ordered presentation queue, between display-list execution.
Other platforms retain the original path. MHF_GE_SYNC is not supported by this
presentation hook; use the normal asynchronous worker.
Two CDRAM buffers avoid reusing the displayed scanout buffer before vblank.
PSP formats 565/5551/4444/8888 convert to opaque Vita A8B8G8R8, scaled 2x nearest
from 480x272 to 960x544. Alpha is ignored for scanout. Bounds/stride are checked.
Controller state is fed on the scheduler side; Start+Select still requests exit.
No audio output frontend or GPU acceleration is added. Existing 20-second
cooperative limit and independent 30-second watchdog are retained.

Validated: pixel color conversion (all formats, channel extremes and 8888 order),
patch application and repeat protection, patched GE C++ syntax with __vita__.
Not validated: native Vita display compilation, physical execution, visual output,
performance or game progression. Build locally before installing. The [present]
log records submitted frames; it does not establish correct game rendering.
No game code/assets or compiled binaries are included.
