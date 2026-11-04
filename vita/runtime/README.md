# Full Runtime diagnostic for Vita

This workbench builds the upstream Runtime, HLE modules, PSP kernel,
interpreter and GE code. The optional Pocket Probe 0.5 executable uses these
implementations, not the reduced Runtime from `vita/src/scheduler_check.cpp`.
It runs only a self-authored Allegrex fixture. No game code or assets are linked.

## Build

Use VitaSDK and a host C++20 compiler. From the repository root:

```sh
cmake -S vita -B build/probe-host -DPOCKET_HOST=ON
cmake --build build/probe-host --target pocket-fixture-generator -j2
export VITASDK=/path/to/vitasdk
export PATH="$VITASDK/bin:$PATH"
sh vita/runtime/build_ffmpeg.sh /absolute/path/to/ffmpeg-7.1.5 \
  build/ffmpeg-vita-abi user_data/deps/ffmpeg-vita-abi
cmake -S vita/runtime -B build/full-runtime-vita \
  -DPOCKET_FFMPEG_PREFIX="$PWD/user_data/deps/ffmpeg-vita-abi" \
  -DPOCKET_FIXTURE_GENERATOR="$PWD/build/probe-host/pocket-fixture-generator" \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build/full-runtime-vita --target pocket-full-runtime.vpk-vpk -j4
```

FFmpeg source: https://ffmpeg.org/releases/ffmpeg-7.1.5.tar.xz
Archive SHA-256:
`de668509caf9e35e3cd162473441fdb29538c6d96ed080292b3cf9e6fc5d558f`.
Verify this before extraction. `build_ffmpeg.sh` uses the unmodified source,
builds h264/atrac3/atrac3p decoders with pthreads and `-fno-short-enums`, and
installs static ARM libraries. All linked project targets use the matching enum
ABI. ASM is disabled for this first portability build; performance is unmeasured.
The earlier prefixes `ffmpeg-vita` and `ffmpeg-vita-threaded` are not valid inputs.

Omit `POCKET_FIXTURE_GENERATOR` to build only `mhf_psp_runtime`. A static archive
alone does not establish final symbol resolution or execution.

## Validation on 2026-11-02

VitaSDK GCC 15.2.0: FFmpeg build/install, full Runtime compile, executable link,
SELF creation and VPK packaging succeeded. The executable contains the real
Runtime constructor and codec symbols, with no unresolved strong symbols.
VPK ZIP integrity, version 00.50, title ID PKPROBE01 and embedded SELF checked.
The host offline contract and 8 synthetic-disc regression tests passed.
Physical Vita execution of 0.5 is pending; previous 0.4 passed on the user's Vita.

## Scope

- `MHF_PSP_OFFLINE` selects an explicit offline transport. Default desktop
  builds retain AEMU. This Vita entry point forces offline mode, ignores an
  initial server and refuses server selection. No host network sockets or
  network threads are created. Socket creation returns not-connected errors;
  operations on absent sockets return invalid-socket errors.
- The fixture executes generated functions through the real Runtime lookup,
  resolves `sceKernelDelayThread` through the real HLE table, and checks that
  the lower-priority thread runs during the delay. Exit values must be 7/9.
  Entry wrappers observe order; they do not implement imports or scheduling.
- The full Runtime's presence does not mean all its HLE implementations are
  complete, or that codecs, GE and the game's import set have been exercised.
- Thread CPU-time telemetry returns zero on Vita until a native counter is
  validated. This does not replace the scheduler clock.
- `runtime.log` records stages and HLE stderr; `probe.log` records the report.
  Both live under `ux0:data/pocketprobe/`. Triangle repeats; Start+Select exits.
- The VPK uses PKPROBE01 and replaces the earlier diagnostic. The ISO is still
  read-only at `ux0:data/pocketprobe/game.iso`; no game save files are used.

## Host offline contract check

```sh
c++ -std=c++20 -pthread -Iplatforms/psp/runtime/src \
  vita/runtime/test_offline.cpp platforms/psp/runtime/src/adhoc_offline.cpp \
  -o build/test-offline
build/test-offline
```

Game boot, native GPU output, audio/movie playback and quest performance remain
unvalidated. Do not interpret a diagnostic PASS as support for the game or 60 fps.

## Private game compilation

Set `POCKET_GAME_GENERATED` to the local recompiler output folder containing
`mhfu.hpp`, then build target `pocket_game_generated`. This compiles all generated
translation units into an ARM archive with the runtime ABI. It does not package
a game VPK or establish game execution. Generated inputs and outputs remain
ignored and must never be committed or included in source releases.

Target `pocket_game_link_check` references the generated program registry and
registers all overlays with the real Runtime. Building it checks final symbol
resolution across the generated code and runtime. It is an ELF link audit,
not a game runner or an installable VPK. Its ARM syntax check passes; final
linking remains pending completion of the generated-code compilation.
