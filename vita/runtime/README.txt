Pocket Boot Test — source addon, experimental

This adds a separate, opt-in VPK target for the player's local generated code.
No game assets or generated game code are included. Apply from the project root:
python3 vita/runtime/apply_boot.py
Configure with the existing POCKET_GAME_GENERATED and POCKET_FFMPEG_PREFIX.
Build target pocket-game-boot.vpk-vpk. This should reuse compiled game objects.

Not yet cross-compiled or tested on physical Vita. Native GPU output and audio
frontend are absent. Expect a blank screen if launched. The purpose is to record
entry/loading/import failures in ux0:data/pocketprobe/boot.log. The timeout is
cooperative (20 seconds); a guest loop that does not yield can prevent it.
Use the PS button to close a hung application. START+SELECT requests exit when
host polling is reached. Existing PSP saves are not used: the runner has its own
boot-ms directory. Code logging may grow quickly during the short experiment.

Required player files (do not bundle into this addon):
ux0:data/pocketprobe/game.iso — unmodified ULES01213 version 1.01
ux0:data/pocketprobe/prepared.elf — generated locally by prepare-local
Optional fonts: ux0:data/pocketprobe/fonts/ (free fonts from the source project).

The enum linker warning is not suppressed or resolved by this addon. Project
and FFmpeg must retain -fno-short-enums. VitaSDK ABI boundaries still need audit.
Successful VPK generation or a scheduler return does not prove game boot.
Source addon distributed under GPL-3.0-or-later, as the project.
