#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
BASE="$ROOT/ps3-resource-check/shipwright-core-ps3-resources.o"
BASE_UNDEFINED="$ROOT/ps3-resource-check/undefined.txt"
OUT="$ROOT/native-link-check"
CC_BIN="${OOT_PS3_CC:-ppu-gcc}"
CXX_BIN="${OOT_PS3_CXX:-ppu-g++}"
LD_BIN="${OOT_PS3_LD:-ppu-ld}"
AR_BIN="${OOT_PS3_AR:-ppu-ar}"
NM_BIN="${OOT_PS3_NM:-ppu-nm}"
OBJCOPY_BIN="${OOT_PS3_OBJCOPY:-ppu-objcopy}"
READELF_BIN="${OOT_PS3_READELF:-ppu-readelf}"
PS3_SDK_ROOT="${PSL1GHT:-${PS3DEV:-/usr/local/ps3dev}}"
mkdir -p "$OUT/objects" "$OUT/logs"
REPORT="$OUT/summary.txt"

main() {
echo 'V46 native boot-link check (first real game executable attempt)'
for tool in "$CC_BIN" "$CXX_BIN" "$LD_BIN" "$AR_BIN" "$NM_BIN" "$OBJCOPY_BIN" "$READELF_BIN"; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
cc_run=("$CC_BIN" -mminimal-toc)
cxx_run=("$CXX_BIN" -mminimal-toc)
if [ "${OOT_PS3_NO_CCACHE:-0}" != 1 ] && command -v ccache >/dev/null 2>&1; then
    cc_run=(ccache "$CC_BIN" -mminimal-toc)
    cxx_run=(ccache "$CXX_BIN" -mminimal-toc)
    echo 'Compiler cache enabled for repeated PS3 C/C++ objects'
fi
if [ ! -f "$BASE" ] || [ ! -f "$BASE_UNDEFINED" ]; then
    echo 'Rebuilding native OS/resource prerequisites...'
    bash "$ROOT/scripts/check-real-ps3-os.sh" >"$OUT/logs/prereq-os.log" 2>&1 || {
        echo 'BLOCKED rebuilding PS3 OS prerequisite'; tail -n 8 "$OUT/logs/prereq-os.log"; exit 2; }
    bash "$ROOT/scripts/check-real-ps3-resources.sh" >"$OUT/logs/prereq-resources.log" 2>&1 || {
        echo 'BLOCKED rebuilding resource prerequisite'; tail -n 8 "$OUT/logs/prereq-resources.log"; exit 2; }
fi

# Resource globals are deliberately lazy because project constructor tables
# are disabled in this probe.  Refresh these three inexpensive bridge objects
# on every native relink so resource/bootstrap edits never require a 696-file
# C-core rebuild.
echo 'Refreshing PS3 resource bridge (3 objects)...'
bash "$ROOT/scripts/check-real-ps3-resources.sh" >"$OUT/logs/refresh-resources.log" 2>&1 || {
    echo 'BLOCKED refreshing PS3 resource bridge'
    tail -n 8 "$OUT/logs/refresh-resources.log"
    exit 2
}
echo 'Fresh constructor-free resource bridge compiled'

common=(-O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror -Wno-unknown-pragmas
    -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 -DIS_BIGENDIAN
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6
    "-I$ROOT/ps3_platform/include" "-I$ROOT/upstream_compat/include"
    "-I$UPSTREAM/soh" "-I$UPSTREAM/soh/include" "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/soh" "-I$UPSTREAM/soh/assets"
    "-I$UPSTREAM/libultraship/include" "-I$UPSTREAM/libultraship/src"
    "-I$PS3_SDK_ROOT/ppu/include")

echo 'Compiling native bootstrap, RSX transport, and real N64 audio mixer...'
bash "$ROOT/scripts/build-fast3d-rsx-shaders.sh" >"$OUT/logs/fast3d-rsx-shaders.log" 2>&1 || {
    echo 'BLOCKED compiling RSX vertex/fragment shaders'
    tail -n 8 "$OUT/logs/fast3d-rsx-shaders.log"
    exit 3
}
"${cxx_run[@]}" -std=gnu++17 "${common[@]}" -c "$ROOT/ps3_platform/source/shipwright_bootstrap_ps3.cpp" \
    -o "$OUT/objects/bootstrap.o" >"$OUT/logs/bootstrap.log" 2>&1 || {
    echo 'BLOCKED compiling native bootstrap'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/bootstrap.log"; exit 3; }
"${cxx_run[@]}" -std=gnu++17 "${common[@]}" -Wno-unused-parameter \
    -c "$ROOT/ps3_platform/source/shipwright_vanilla_ps3.cpp" \
    -o "$OUT/objects/vanilla.o" >"$OUT/logs/vanilla.log" 2>&1 || {
    echo 'BLOCKED compiling vanilla feature bridge'; grep -m4 -E 'fatal error:|error:' "$OUT/logs/vanilla.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 "${common[@]}" -c "$ROOT/ps3_platform/source/rsx_screen.c" \
    -o "$OUT/objects/rsx_screen.o" >"$OUT/logs/rsx.log" 2>&1 || {
    echo 'BLOCKED compiling RSX transport'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/rsx.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 "${common[@]}" "-I$OUT" \
    -c "$ROOT/ps3_platform/source/fast3d_rsx_ps3.c" \
    -o "$OUT/objects/fast3d_rsx_ps3.o" >"$OUT/logs/fast3d-rsx.log" 2>&1 || {
    echo 'BLOCKED compiling real RSX Fast3D vertex/texture backend'
    grep -m5 -E 'fatal error:|error:' "$OUT/logs/fast3d-rsx.log" || tail -n 6 "$OUT/logs/fast3d-rsx.log"
    exit 3
}
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_process_param.c" \
    -o "$OUT/objects/ps3_process_param.o" >"$OUT/logs/process-param.log" 2>&1 || {
    echo 'BLOCKED compiling PS3 process metadata'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/process-param.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_sbrk_fixed.c" \
    -o "$OUT/objects/ps3_sbrk_fixed.o" >"$OUT/logs/sbrk-fixed.log" 2>&1 || {
    echo 'BLOCKED compiling fixed PS3 heap provider'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/sbrk-fixed.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_game_heaps.c" \
    -o "$OUT/objects/ps3_game_heaps.o" >"$OUT/logs/game-heaps.log" 2>&1 || {
    echo 'BLOCKED compiling native OoT game heaps'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/game-heaps.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_sys_cfb.c" \
    -o "$OUT/objects/ps3_sys_cfb.o" >"$OUT/logs/sys-cfb.log" 2>&1 || {
    echo 'BLOCKED compiling native PS3 color framebuffers'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/sys-cfb.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_newlib_lock.c" \
    -o "$OUT/objects/ps3_newlib_lock.o" >"$OUT/logs/newlib-lock.log" 2>&1 || {
    echo 'BLOCKED compiling constructor-free newlib lock provider'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/newlib-lock.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_glob_fixed.c" \
    -o "$OUT/objects/ps3_glob_fixed.o" >"$OUT/logs/glob-fixed.log" 2>&1 || {
    echo 'BLOCKED compiling constructor-free PS3 cwd provider'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/glob-fixed.log"; exit 3; }
# PS3 audio output (libaudio port + audio thread); system headers only, no game headers.
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_audio.c" \
    -o "$OUT/objects/ps3_audio.o" >"$OUT/logs/ps3-audio.log" 2>&1 || {
    echo 'BLOCKED compiling PS3 audio output'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-audio.log"; exit 3; }
# PS3 system save data (XMB Saved Data Utility); system headers only, no game headers.
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_savedata.c" \
    -o "$OUT/objects/ps3_savedata.o" >"$OUT/logs/ps3-savedata.log" 2>&1 || {
    echo 'BLOCKED compiling PS3 save data'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-savedata.log"; exit 3; }
# Debug menu overlay (R3); system headers only, no game headers.
# Release builds: OOT_PS3_DEBUG_MENU=0 compiles the menu out (R3 does nothing).
echo "Debug menu (R3): ${OOT_PS3_DEBUG_MENU:-1}"
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-DOOT_PS3_DEBUG_MENU=${OOT_PS3_DEBUG_MENU:-1}" "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_debug_overlay.c" \
    -o "$OUT/objects/ps3_debug_overlay.o" >"$OUT/logs/ps3-debug-overlay.log" 2>&1 || {
    echo 'BLOCKED compiling debug menu overlay'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-debug-overlay.log"; exit 3; }
# First-boot setup screen (installs oot.o2r from USB, XMB icon via portlibs zlib).
PS3_PORTLIBS="${PS3DEV:-/usr/local/ps3dev}/portlibs/ppu"
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-DOOT_PS3_PORT_VERSION=\"$(grep -m1 '^PORT_VERSION=' "$ROOT/scripts/package-real-native.sh" | cut -d'"' -f2)\"" \
    "-I$PS3_SDK_ROOT/ppu/include" "-I$PS3_PORTLIBS/include" \
    -c "$ROOT/ps3_platform/source/ps3_setup_screen.c" \
    -o "$OUT/objects/ps3_setup_screen.o" >"$OUT/logs/ps3-setup-screen.log" 2>&1 || {
    echo 'BLOCKED compiling setup screen'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-setup-screen.log"; exit 3; }
# SPU audio mixer: the SPU program (spu-gcc), embedded as a C array, and its PPU driver.
SPU_GCC="${PS3_SDK_ROOT}/spu/bin/spu-gcc"
[ -x "$SPU_GCC" ] || SPU_GCC="$(command -v spu-gcc || true)"
[ -n "$SPU_GCC" ] || { echo 'BLOCKED: spu-gcc is required for the SPU audio mixer'; exit 3; }
"$SPU_GCC" -O2 -Wall -Wno-memset-elt-size -Werror -mdual-nops -fmodulo-sched -ffunction-sections -fdata-sections \
    "-I$PS3_SDK_ROOT/spu/include" "$ROOT/ps3_platform/spu/oot_audio_spu.c" -o "$OUT/objects/oot_audio_spu.elf" \
    "-L$PS3_SDK_ROOT/spu/lib" -Wl,--gc-sections -lsputhread >"$OUT/logs/spu-audio.log" 2>&1 || {
    echo 'BLOCKED compiling SPU audio mixer'; grep -m3 -E 'error' "$OUT/logs/spu-audio.log"; exit 3; }
python3 - "$OUT/objects/oot_audio_spu.elf" "$OUT/oot-audio-spu-bin.h" <<'PY'
import pathlib, sys
data = pathlib.Path(sys.argv[1]).read_bytes()
with open(sys.argv[2], 'w') as out:
    out.write('/* Generated from ps3_platform/spu/oot_audio_spu.c (SPU ELF). */\n')
    out.write('static const unsigned char oot_audio_spu_bin[] __attribute__((aligned(128))) = {\n')
    for i in range(0, len(data), 16):
        out.write('    ' + ','.join('0x%02x' % b for b in data[i:i+16]) + ',\n')
    out.write('};\n')
PY
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" "-I$OUT" \
    -c "$ROOT/ps3_platform/source/ps3_spu_audio.c" \
    -o "$OUT/objects/ps3_spu_audio.o" >"$OUT/logs/ps3-spu-audio.log" 2>&1 || {
    echo 'BLOCKED compiling SPU audio driver'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-spu-audio.log"; exit 3; }
# Heap guard (diagnostic malloc wrappers); system headers only.
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -Wno-frame-address -c "$ROOT/ps3_platform/source/ps3_heap_guard.c" \
    -o "$OUT/objects/ps3_heap_guard.o" >"$OUT/logs/ps3-heap-guard.log" 2>&1 || {
    echo 'BLOCKED compiling heap guard'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/ps3-heap-guard.log"; exit 3; }
# Third-party stb_image (JPEG only) for pre-rendered backgrounds; its own warnings are not ours.
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -ffunction-sections -fdata-sections -w \
    -c "$ROOT/ps3_platform/source/ps3_jpeg.c" \
    -o "$OUT/objects/ps3_jpeg.o" >"$OUT/logs/ps3-jpeg.log" 2>&1 || {
    echo 'BLOCKED compiling JPEG decoder'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/ps3-jpeg.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    -c "$ROOT/ps3_platform/source/ps3_ctor_mid105.c" \
    -o "$OUT/objects/ps3_ctor_mid105.o" >"$OUT/logs/ctor-mid105.log" 2>&1 || {
    echo 'BLOCKED compiling priority-105 midpoint marker'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/ctor-mid105.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    "-I$PS3_SDK_ROOT/ppu/include" \
    "-DOOT_PS3_APP_VER=\"$(grep -m1 '^PORT_VERSION=' "$ROOT/scripts/package-real-native.sh" | cut -d'"' -f2)\"" \
    -c "$ROOT/ps3_platform/source/ps3_early_entry.c" \
    -o "$OUT/objects/ps3_early_entry.o" >"$OUT/logs/early-entry.log" 2>&1 || {
    echo 'BLOCKED compiling PS3 early entry'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/early-entry.log"; exit 3; }
early_ctor_sections=$("$READELF_BIN" -SW "$OUT/objects/ps3_early_entry.o" 2>/dev/null)
if ! grep -E '\.ctors\.65434' <<<"$early_ctor_sections" >/dev/null ||
   ! grep -E '\.ctors\.65433' <<<"$early_ctor_sections" >/dev/null ||
   ! grep -E '\.ctors\.65431z' <<<"$early_ctor_sections" >/dev/null ||
   ! grep -E '\.ctors\.65430z' <<<"$early_ctor_sections" >/dev/null ||
   ! grep -E '\.ctors\.65429' <<<"$early_ctor_sections" >/dev/null; then
    echo 'BLOCKED: raw constructor ladder sections were not emitted as expected'
    exit 3
fi
echo 'Raw constructor ladder: P101/P102/P103/P104/P106/default emitted'
"${cc_run[@]}" -std=gnu11 "${common[@]}" -c "$ROOT/ps3_platform/source/shipwright_video_ps3.c" \
    -o "$OUT/objects/shipwright_video.o" >"$OUT/logs/shipwright-video.log" 2>&1 || {
    echo 'BLOCKED compiling Shipwright video bridge'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/shipwright-video.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 "${common[@]}" -c "$ROOT/ps3_platform/source/software_renderer.c" \
    -o "$OUT/objects/software_renderer.o" >"$OUT/logs/renderer.log" 2>&1 || {
    echo 'BLOCKED compiling software renderer'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/renderer.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 "${common[@]}" -Wno-unused-function -Wno-unused-parameter \
    -c "$UPSTREAM/soh/soh/mixer.c" \
    -o "$OUT/objects/mixer.o" >"$OUT/logs/mixer.log" 2>&1 || {
    echo 'BLOCKED compiling real audio mixer'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/mixer.log"; exit 3; }
"${cc_run[@]}" -std=gnu11 "${common[@]}" -Wno-unused-function \
    -c "$ROOT/ps3_platform/source/shipwright_os_ps3.c" \
    -o "$OUT/objects/shipwright_os_ps3.o" >"$OUT/logs/shipwright-os.log" 2>&1 || {
    echo 'BLOCKED compiling fresh atomic PS3 OS bridge'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/shipwright-os.log"; exit 3; }
echo 'Fresh atomic PS3 OS bridge compiled'

# Recompile only the upstream game entry object.  This avoids rebuilding all
# 696 C translation units whenever the hardware boot checkpoints change.
game_main_flags=(-std=gnu11 -O2 -fno-strict-aliasing -mcpu=cell -ffunction-sections -fdata-sections
    -D__PS3__ -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6
    "-I$ROOT/ps3_platform/include"
    "-I$UPSTREAM/soh" "-I$UPSTREAM/soh/include" "-I$UPSTREAM/soh/src"
    "-I$UPSTREAM/soh/soh" "-I$UPSTREAM/soh/assets"
    "-I$UPSTREAM/libultraship/include" "-I$UPSTREAM/libultraship/src")
game_main_cvars=(RANDOMIZER_ENHANCEMENT=gRandoEnhancements RANDOMIZER_SETTING=gRandoSettings
    COSMETIC=gCosmetics AUDIO=gAudioEditor CHEAT=gCheats ENHANCEMENT=gEnhancements
    SETTING=gSettings WINDOW=gOpenWindows TRACKER=gTrackers
    DEVELOPER_TOOLS=gDeveloperTools GENERAL=gGeneral REMOTE=gRemote
    GAMEPLAY_STATS=gGameplayStats TIME_DISPLAY=gTimeDisplay)
for cvar in "${game_main_cvars[@]}"; do
    game_main_flags+=("-DCVAR_PREFIX_${cvar%%=*}=\"${cvar#*=}\"")
done
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_room.c" \
    -o "$OUT/objects/game-room-draw.o" >"$OUT/logs/game-room-draw.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented upstream Room_Draw'
    grep -m4 -E 'fatal error:|error:' "$OUT/logs/game-room-draw.log" || tail -n 6 "$OUT/logs/game-room-draw.log"
    exit 3
}
if ! strings "$OUT/objects/game-room-draw.o" | grep -F 'room-draw: before handler dispatch' >/dev/null; then
    echo 'BLOCKED: fresh upstream Room_Draw trace marker absent'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_scene_table.c" \
    -o "$OUT/objects/game-scene-draw.o" >"$OUT/logs/game-scene-draw.log" 2>&1 || {
    echo 'BLOCKED compiling upstream Scene_Draw graphics setup'
    grep -m4 -E 'fatal error:|error:' "$OUT/logs/game-scene-draw.log" || tail -n 6 "$OUT/logs/game-scene-draw.log"
    exit 3
}
if ! strings "$OUT/objects/game-scene-draw.o" | grep -F 'scene-draw: upstream graphics segment setup completed' >/dev/null; then
    echo 'BLOCKED: fresh upstream Scene_Draw trace marker absent'
    exit 3
fi
"${cxx_run[@]}" -std=gnu++17 "${game_main_flags[@]:1}" \
    -c "$UPSTREAM/soh/soh/ActorDB.cpp" \
    -o "$OUT/objects/game-actor-db.o" >"$OUT/logs/game-actor-db.log" 2>&1 || {
    echo 'BLOCKED compiling constructor-safe ActorDB'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-actor-db.log"; exit 3; }
echo 'Fresh constructor-safe ActorDB compiled'
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/__osMalloc.c" \
    -o "$OUT/objects/game-arena.o" >"$OUT/logs/game-arena.log" 2>&1 || {
    echo 'BLOCKED compiling native-safe OoT arena'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-arena.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/system_malloc.c" \
    -o "$OUT/objects/game-system-malloc.o" >"$OUT/logs/game-system-malloc.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented system arena'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-system-malloc.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/code_800FC620.c" \
    -o "$OUT/objects/game-system-heap.o" >"$OUT/logs/game-system-heap.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented system heap'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-system-heap.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/audioMgr.c" \
    -o "$OUT/objects/game-audio-mgr.o" >"$OUT/logs/game-audio-mgr.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented audio manager'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-audio-mgr.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/padmgr.c" \
    -o "$OUT/objects/game-pad-mgr.o" >"$OUT/logs/game-pad-mgr.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented pad manager'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-pad-mgr.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/graph.c" \
    -o "$OUT/objects/game-graph.o" >"$OUT/logs/game-graph.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented graph thread'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-graph.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/game.c" \
    -o "$OUT/objects/game-state-init.o" >"$OUT/logs/game-state-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented game state'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-state-init.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_play.c" \
    -o "$OUT/objects/game-play-init.o" >"$OUT/logs/game-play-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented Play state'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-play-init.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_actor.c" \
    -o "$OUT/objects/game-actor-init.o" >"$OUT/logs/game-actor-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented actor initialization'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-actor-init.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/actors/ovl_En_Horse/z_en_horse.c" \
    -o "$OUT/objects/game-horse-init.o" >"$OUT/logs/game-horse-init.log" 2>&1 || {
    echo 'BLOCKED compiling genuine horse initialization'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-horse-init.log"; exit 3; }
if ! strings "$OUT/objects/game-horse-init.o" | grep -F 'intro-horse-init: before real horse Skin_Init' >/dev/null; then
    echo 'BLOCKED: fresh horse actor initialization trace missing from compiled object'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_skin_awb.c" \
    -o "$OUT/objects/game-skin-init.o" >"$OUT/logs/game-skin-init.log" 2>&1 || {
    echo 'BLOCKED compiling native horse skin initialization'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-skin-init.log"; exit 3; }
if ! strings "$OUT/objects/game-skin-init.o" | grep -F 'intro-skin: genuine skinned animation initialized' >/dev/null; then
    echo 'BLOCKED: fresh native skin initialization trace missing from compiled object'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_horse.c" \
    -o "$OUT/objects/game-horse-setup.o" >"$OUT/logs/game-horse-setup.log" 2>&1 || {
    echo 'BLOCKED compiling genuine horse mount and camera setup'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-horse-setup.log"; exit 3; }
if ! strings "$OUT/objects/game-horse-setup.o" | grep -F 'intro-horse-setup: after horse camera request' >/dev/null; then
    echo 'BLOCKED: fresh genuine horse mount trace missing from compiled object'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/actors/ovl_player_actor/z_player.c" \
    -o "$OUT/objects/game-player-init.o" >"$OUT/logs/game-player-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented player initialization'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-player-init.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_player_lib.c" \
    -o "$OUT/objects/game-player-draw.o" >"$OUT/logs/game-player-draw.log" 2>&1 || {
    echo 'BLOCKED compiling real Link limb draw path'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-player-draw.log"; exit 3; }
if ! strings "$OUT/objects/game-player-draw.o" | grep -F 'player-draw-impl: before real skeleton traversal' >/dev/null; then
    echo 'BLOCKED: fresh real Link draw probes missing from compiled object'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_skelanime.c" \
    -o "$OUT/objects/game-skelanime-init.o" >"$OUT/logs/game-skelanime-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented skeleton initialization'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-skelanime-init.log"; exit 3; }
if ! strings "$OUT/objects/game-skelanime-init.o" | grep -F 'skel-init-link: before archive skeleton load' >/dev/null; then
    echo 'BLOCKED: skeleton initialization trace marker missing from fresh object'
    exit 3
fi
if ! strings "$OUT/objects/game-player-init.o" | grep -F 'player-init-common: before first SkelAnime_InitLink' >/dev/null; then
    echo 'BLOCKED: player initialization trace marker missing from fresh object'
    exit 3
fi
if ! strings "$OUT/objects/game-actor-init.o" | grep -F 'actor-init: before Actor_SpawnEntry' >/dev/null; then
    echo 'BLOCKED: actor initialization trace marker missing from fresh object'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_demo.c" \
    -o "$OUT/objects/game-cutscene-conditional.o" >"$OUT/logs/game-cutscene-conditional.log" 2>&1 || {
    echo 'BLOCKED compiling cutscene conditional probe'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-cutscene-conditional.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_construct.c" \
    -o "$OUT/objects/game-message-init.o" >"$OUT/logs/game-message-init.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented message initializer'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-message-init.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_message_PAL.c" \
    -o "$OUT/objects/game-message-tables.o" >"$OUT/logs/game-message-tables.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented message tables'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-message-tables.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_kankyo.c" \
    -o "$OUT/objects/game-kankyo.o" >"$OUT/logs/game-kankyo.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented environment/kankyo'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-kankyo.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_skin.c" \
    -o "$OUT/objects/game-skin.o" >"$OUT/logs/game-skin.log" 2>&1 || {
    echo 'BLOCKED compiling skin renderer'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-skin.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/code_800BB0A0.c" \
    -o "$OUT/objects/game-spline.o" >"$OUT/logs/game-spline.log" 2>&1 || {
    echo 'BLOCKED compiling camera spline'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-spline.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_bgcheck.c" \
    -o "$OUT/objects/game-bgcheck.o" >"$OUT/logs/game-bgcheck.log" 2>&1 || {
    echo 'BLOCKED compiling collision engine'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-bgcheck.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/actors/ovl_En_Mag/z_en_mag.c" \
    -o "$OUT/objects/game-en-mag.o" >"$OUT/logs/game-en-mag.log" 2>&1 || {
    echo 'BLOCKED compiling title logo actor'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-en-mag.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$ROOT/ps3_platform/source/ps3_debug_game.c" \
    -o "$OUT/objects/game-ps3-debug.o" >"$OUT/logs/game-ps3-debug.log" 2>&1 || {
    echo 'BLOCKED compiling debug menu game helpers'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-ps3-debug.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_kanfont.c" \
    -o "$OUT/objects/game-kanfont.o" >"$OUT/logs/game-kanfont.log" 2>&1 || {
    echo 'BLOCKED compiling font loader'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-kanfont.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_effect_soft_sprite.c" \
    -o "$OUT/objects/game-effect-ss.o" >"$OUT/logs/game-effect-ss.log" 2>&1 || {
    echo 'BLOCKED compiling soft sprite effects'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-effect-ss.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/effects/ovl_Effect_Ss_KiraKira/z_eff_ss_kirakira.c" \
    -o "$OUT/objects/game-eff-kirakira.o" >"$OUT/logs/game-eff-kirakira.log" 2>&1 || {
    echo 'BLOCKED compiling sparkle effect'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-eff-kirakira.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_file_choose/z_file_choose.c" \
    -o "$OUT/objects/game-fc-z-file-choose.o" >"$OUT/logs/game-fc-z-file-choose.log" 2>&1 || {
    echo 'BLOCKED compiling file select (z_file_choose)'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-fc-z-file-choose.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_file_choose/z_file_copy_erase.c" \
    -o "$OUT/objects/game-fc-z-file-copy-erase.o" >"$OUT/logs/game-fc-z-file-copy-erase.log" 2>&1 || {
    echo 'BLOCKED compiling file select (z_file_copy_erase)'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-fc-z-file-copy-erase.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_NES.c" \
    -o "$OUT/objects/game-fc-z-file-nameset-NES.o" >"$OUT/logs/game-fc-z-file-nameset-NES.log" 2>&1 || {
    echo 'BLOCKED compiling file select (z_file_nameset_NES)'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-fc-z-file-nameset-NES.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_PAL.c" \
    -o "$OUT/objects/game-fc-z-file-nameset-PAL.o" >"$OUT/logs/game-fc-z-file-nameset-PAL.log" 2>&1 || {
    echo 'BLOCKED compiling file select (z_file_nameset_PAL)'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-fc-z-file-nameset-PAL.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_file_choose/z_file_nameset_data.c" \
    -o "$OUT/objects/game-fc-z-file-nameset-data.o" >"$OUT/logs/game-fc-z-file-nameset-data.log" 2>&1 || {
    echo 'BLOCKED compiling file select (z_file_nameset_data)'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-fc-z-file-nameset-data.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_vr_box.c" \
    -o "$OUT/objects/game-z-vr-box.o" >"$OUT/logs/game-z-vr-box.log" 2>&1 || {
    echo 'BLOCKED compiling z_vr_box.c'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-z-vr-box.log"; exit 3; }
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/z_vr_box_draw.c" \
    -o "$OUT/objects/game-z-vr-box-draw.o" >"$OUT/logs/game-z-vr-box-draw.log" 2>&1 || {
    echo 'BLOCKED compiling z_vr_box_draw.c'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-z-vr-box-draw.log"; exit 3; }
if ! strings "$OUT/objects/game-state-init.o" | grep -F 'game-init: before graphics queue send' >/dev/null ||
   ! strings "$OUT/objects/game-play-init.o" | grep -F 'play-init: entrance lookup completed' >/dev/null ||
   ! strings "$OUT/objects/game-cutscene-conditional.o" | grep -F 'cutscene-conditional: PS3 diagnostic skips uninitialized hook' >/dev/null ||
   ! strings "$OUT/objects/game-cutscene-conditional.o" | grep -F 'cutscene-entrance: PS3 diagnostic skips uninitialized hook' >/dev/null ||
   ! strings "$OUT/objects/game-message-init.o" | grep -F 'message-init: NTSC font table unavailable; diagnostic skip' >/dev/null ||
   ! strings "$OUT/objects/game-message-init.o" | grep -F 'interface-init: before parameter DMA' >/dev/null ||
   ! strings "$OUT/objects/game-message-tables.o" | grep -F 'message-tables: before OTRMessage_Init' >/dev/null; then
    echo 'BLOCKED: fresh state/Play/message objects are missing PS3 trace markers'
    exit 3
fi
if ! strings "$OUT/objects/game-graph.o" | grep -F 'graph: RunFrame entered' >/dev/null; then
    echo 'BLOCKED: instrumented graph object is missing the PS3 trace marker'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/overlays/gamestates/ovl_title/z_title.c" \
    -o "$OUT/objects/game-title.o" >"$OUT/logs/game-title.log" 2>&1 || {
    echo 'BLOCKED compiling instrumented title gamestate'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-title.log"; exit 3; }
if ! strings "$OUT/objects/game-title.o" | grep -F 'title-draw: real O2R Nintendo 64 logo display list emitted' >/dev/null; then
    echo 'BLOCKED: instrumented title object is missing the PS3 trace marker'
    exit 3
fi
"${cc_run[@]}" "${game_main_flags[@]}" \
    -c "$UPSTREAM/soh/src/code/main.c" \
    -o "$OUT/objects/game-main.o" >"$OUT/logs/game-main.log" 2>&1 || {
    echo 'BLOCKED compiling fresh Shipwright game entry'; grep -m3 -E 'fatal error:|error:' "$OUT/logs/game-main.log"; exit 3; }
echo 'Fresh Shipwright game entry/audio/pad/graph/title managers compiled; diagnostic RSX prelude bypassed'

# Build the actual upstream Fast3D interpreter and PS3 rendering/resource
# backend for this ELF. A syntax-only check or an older object is insufficient.
if ! OOT_PS3_FAST3D_OBJECT_MODE=1 bash "$ROOT/scripts/check-ps3-fast3d-interpreter.sh" \
    >"$OUT/logs/fast3d-build.log" 2>&1; then
    echo 'BLOCKED compiling native Fast3D rendering path'
    tail -n 9 "$OUT/logs/fast3d-build.log"
    exit 3
fi
echo 'Fresh native Fast3D interpreter, PS3 O2R adapter, and renderer compiled'

objects=("$OUT/objects/bootstrap.o" "$OUT/objects/vanilla.o" "$OUT/objects/rsx_screen.o" "$OUT/objects/fast3d_rsx_ps3.o" "$OUT/objects/ps3_process_param.o" "$OUT/objects/ps3_sbrk_fixed.o" "$OUT/objects/ps3_newlib_lock.o" "$OUT/objects/ps3_glob_fixed.o" "$OUT/objects/ps3_early_entry.o" "$OUT/objects/ps3_jpeg.o" "$OUT/objects/ps3_audio.o" "$OUT/objects/ps3_savedata.o" "$OUT/objects/ps3_debug_overlay.o" "$OUT/objects/ps3_setup_screen.o" "$OUT/objects/ps3_heap_guard.o" "$OUT/objects/ps3_spu_audio.o" "$OUT/objects/shipwright_video.o" \
    "$OUT/objects/software_renderer.o" "$OUT/objects/mixer.o")
if ! "$LD_BIN" -r -o "$OUT/shipwright-native-bootstrap.o" "$BASE" "${objects[@]}" >"$OUT/logs/reloc-link.log" 2>&1; then
    echo 'BLOCKED: native bootstrap relocatable link failed'
    grep -m4 -E 'multiple definition|undefined reference|error:' "$OUT/logs/reloc-link.log" || true
    exit 4
fi
"$NM_BIN" -u "$OUT/shipwright-native-bootstrap.o" | awk 'NF { print $NF }' | sort -u >"$OUT/undefined-reloc.txt"
comm -23 "$BASE_UNDEFINED" "$OUT/undefined-reloc.txt" >"$OUT/resolved.txt"
echo "Project/SDK symbols resolved before final link: $(wc -l < "$OUT/resolved.txt")"
echo "Relocatable frontier: $(wc -l < "$OUT/undefined-reloc.txt")"

libdir="$PS3_SDK_ROOT/ppu/powerpc64-ps3-elf/lib"
sdk_libdir="$PS3_SDK_ROOT/ppu/lib"

# GCC 7's eh_alloc.o constructs a dynamically allocated emergency exception
# pool at default constructor priority.  That is the first constructor after
# the now-fixed PSL1GHT priority-105 group, and it crashes before main on HEN.
# Keep GCC's exact exception ABI implementation, but link a copy with only its
# pre-main initializer table removed.  Normal malloc-backed exception objects
# continue to work; only the out-of-memory fallback pool is unavailable in
# this diagnostic executable.
libstdcxx_path=$("$CXX_BIN" -print-file-name=libstdc++.a)
if [ ! -f "$libstdcxx_path" ]; then
    echo "BLOCKED: libstdc++.a not found at $libstdcxx_path"
    exit 4
fi
EH_ALLOC_DIR="$OUT/eh-alloc-noctor"
rm -rf "$EH_ALLOC_DIR"
mkdir -p "$EH_ALLOC_DIR"
(
    cd "$EH_ALLOC_DIR"
    "$AR_BIN" x "$libstdcxx_path" eh_alloc.o
)
if [ ! -f "$EH_ALLOC_DIR/eh_alloc.o" ]; then
    echo 'BLOCKED: eh_alloc.o was not found in PS3 libstdc++.a'
    exit 4
fi
mapfile -t eh_init_sections < <(
    "$READELF_BIN" -SW "$EH_ALLOC_DIR/eh_alloc.o" 2>/dev/null |
        sed -n 's/^ *\[[^]]*\] *\(\.ctors[^ ]*\|\.init_array[^ ]*\) .*/\1/p'
)
if [ "${#eh_init_sections[@]}" -eq 0 ]; then
    echo 'BLOCKED: eh_alloc.o constructor section was not detected'
    exit 4
fi
eh_remove_args=()
for section in "${eh_init_sections[@]}"; do
    eh_remove_args+=(-R "$section")
done
"$OBJCOPY_BIN" "${eh_remove_args[@]}" "$EH_ALLOC_DIR/eh_alloc.o" \
    "$EH_ALLOC_DIR/eh_alloc-noctor.o"
if "$READELF_BIN" -SW "$EH_ALLOC_DIR/eh_alloc-noctor.o" 2>/dev/null |
   grep -E '\.(init_array|ctors)' >/dev/null; then
    echo 'BLOCKED: eh_alloc.o constructor stripping failed'
    exit 4
fi
echo 'PS3 GCC exception allocator preserved; emergency-pool constructor disabled'

# Never feed the ld -r aggregate to the executable linker.  Combining hundreds
# of PPU translation units into one relocatable object collapses their separate
# TOCs into a single table and exceeds the signed 16-bit TOC displacement.  By
# linking the original objects, GNU ld's PowerPC64 multi-TOC support can split
# the tables and synthesize TOC-adjusting stubs as intended.
mapfile -t final_objects < <(find \
    "$ROOT/core-c-check/objects" \
    "$ROOT/essential-cpp-check/objects" \
    "$ROOT/ps3-resource-check/objects" \
    -type f -name '*.o' \
    ! -path "$ROOT/core-c-check/objects/src_code___osMalloc.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_audioMgr.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_code_800FC620.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_graph.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_game.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_play.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_room.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_scene_table.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_actor.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_skin_awb.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_horse.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_actors_ovl_En_Horse_z_en_horse.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_skelanime.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_player_lib.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_actors_ovl_player_actor_z_player.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_demo.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_construct.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_message_PAL.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_kankyo.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_skin.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_code_800BB0A0.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_bgcheck.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_actors_ovl_En_Mag_z_en_mag.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_kanfont.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_effect_soft_sprite.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_effects_ovl_Effect_Ss_KiraKira_z_eff_ss_kirakira.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_file_choose_z_file_choose.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_file_choose_z_file_copy_erase.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_file_choose_z_file_nameset_NES.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_file_choose_z_file_nameset_PAL.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_file_choose_z_file_nameset_data.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_vr_box.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_z_vr_box_draw.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_main.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_padmgr.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_system_malloc.o" \
    ! -path "$ROOT/core-c-check/objects/src_code_sys_cfb.o" \
    ! -path "$ROOT/essential-cpp-check/objects/soh_soh_ActorDB.o" \
    ! -path "$ROOT/core-c-check/objects/src_overlays_gamestates_ovl_title_z_title.o" \
    ! -path "$ROOT/core-c-check/objects/src_buffers_heaps.o" | sort)
final_objects+=(
    "$ROOT/ps3-services-check/shipwright_core_services.o"
    "$OUT/objects/shipwright_os_ps3.o"
    "${objects[@]}"
    "$OUT/objects/ps3_game_heaps.o"
    "$OUT/objects/ps3_sys_cfb.o"
    "$OUT/objects/game-arena.o"
    "$OUT/objects/game-system-malloc.o"
    "$OUT/objects/game-system-heap.o"
    "$OUT/objects/game-audio-mgr.o"
    "$OUT/objects/game-pad-mgr.o"
    "$OUT/objects/game-graph.o"
    "$OUT/objects/game-state-init.o"
    "$OUT/objects/game-play-init.o"
    "$OUT/objects/game-room-draw.o"
    "$OUT/objects/game-scene-draw.o"
    "$OUT/objects/game-actor-init.o"
    "$OUT/objects/game-horse-init.o"
    "$OUT/objects/game-skin-init.o"
    "$OUT/objects/game-horse-setup.o"
    "$OUT/objects/game-player-init.o"
    "$OUT/objects/game-player-draw.o"
    "$OUT/objects/game-skelanime-init.o"
    "$OUT/objects/game-actor-db.o"
    "$OUT/objects/game-cutscene-conditional.o"
    "$OUT/objects/game-message-init.o"
    "$OUT/objects/game-message-tables.o"
    "$OUT/objects/game-kankyo.o"
    "$OUT/objects/game-skin.o"
    "$OUT/objects/game-spline.o"
    "$OUT/objects/game-bgcheck.o"
    "$OUT/objects/game-en-mag.o"
    "$OUT/objects/game-kanfont.o"
    "$OUT/objects/game-ps3-debug.o"
    "$OUT/objects/game-effect-ss.o"
    "$OUT/objects/game-eff-kirakira.o"
    "$OUT/objects/game-fc-z-file-choose.o"
    "$OUT/objects/game-fc-z-file-copy-erase.o"
    "$OUT/objects/game-fc-z-file-nameset-NES.o"
    "$OUT/objects/game-fc-z-file-nameset-PAL.o"
    "$OUT/objects/game-fc-z-file-nameset-data.o"
    "$OUT/objects/game-z-vr-box.o"
    "$OUT/objects/game-z-vr-box-draw.o"
    "$OUT/objects/game-title.o"
    "$OUT/objects/game-main.o"
    "$OUT/objects/fast3d-interpreter.o"
    "$OUT/objects/fast3d-resource-ps3.o"
    "$OUT/objects/fast3d-debugger.o"
    "$OUT/objects/fast3d-video-ps3.o"
    "$EH_ALLOC_DIR/eh_alloc-noctor.o"
)
echo "Final multi-TOC link objects: ${#final_objects[@]}"

# Record which individual translation units can run code before main.  This
# makes a follow-up bisect deterministic if the native entry wrapper is not
# reached on hardware.
: >"$OUT/static-initializers.txt"
for object in "${final_objects[@]}"; do
    if "$READELF_BIN" -SW "$object" 2>/dev/null | grep -E '\.(init_array|ctors)' >/dev/null; then
        printf '%s\n' "$object" >>"$OUT/static-initializers.txt"
    fi
done
echo "Objects with pre-main initializers: $(wc -l < "$OUT/static-initializers.txt")"

# Diagnostic boot: copy only objects with project-owned initializer tables and
# remove those tables from the copies.  Their storage remains zero-initialized,
# and all ordinary functions/data remain linked.  This lets the PS3 reach the
# native main wrapper even if a C++ global constructor is the early crash.
NOINIT_DIR="$OUT/noinit-objects"
rm -rf "$NOINIT_DIR"
mkdir -p "$NOINIT_DIR"
final_link_objects=()
stripped_count=0
for object in "${final_objects[@]}"; do
    # Keep the one diagnostic constructor that uses raw LV2 syscalls.  All
    # other project constructor tables remain disabled for this boot probe.
    if [ "$object" = "$OUT/objects/ps3_early_entry.o" ]; then
        echo 'Retaining raw pre-main trace constructor'
        final_link_objects+=("$object")
        continue
    fi
    if "$READELF_BIN" -SW "$object" 2>/dev/null | grep -E '\.(init_array|ctors)' >/dev/null; then
        stripped_count=$((stripped_count + 1))
        clean_object="$NOINIT_DIR/$(printf '%04d' "$stripped_count")_$(basename "$object")"
        mapfile -t init_sections < <(
            "$READELF_BIN" -SW "$object" 2>/dev/null |
                sed -n 's/^ *\[[^]]*\] *\(\.ctors[^ ]*\|\.init_array[^ ]*\) .*/\1/p'
        )
        remove_args=()
        for section in "${init_sections[@]}"; do
            remove_args+=(-R "$section")
        done
        "$OBJCOPY_BIN" "${remove_args[@]}" "$object" "$clean_object"

        # Old PS3 binutils accepts -R with exact names but not wildcard section
        # patterns.  Fail the build if a constructor section survived instead
        # of falsely claiming it was disabled.
        if "$READELF_BIN" -SW "$clean_object" 2>/dev/null | grep -E '\.(init_array|ctors)' >/dev/null; then
            echo "BLOCKED: initializer stripping failed for $object"
            exit 4
        fi
        final_link_objects+=("$clean_object")
    else
        final_link_objects+=("$object")
    fi
done
echo "Project initializer tables disabled: $stripped_count"

# GameInteractor_Execute* walk hook registries built by C++ static constructors,
# which never run on PS3 (no hooks are ever registered), so they can hang.
# Wrap every one (all return void) to a no-op, like GameInteractor_Should.
gi_hooks_object="$ROOT/essential-cpp-check/objects/soh_soh_Enhancements_game-interactor_GameInteractor_Hooks.o"
gi_noop_source="$OUT/objects/ps3_gi_noop.c"
gi_wrap_flags=()
{
    echo '/* Generated by check-real-native-link.sh: no-op GameInteractor hook executors. */'
    while read -r gi_symbol; do
        echo "void __wrap_${gi_symbol}(void);"
        echo "void __wrap_${gi_symbol}(void) {}"
        gi_wrap_flags+=("-Wl,--wrap=${gi_symbol}")
    done < <("$NM_BIN" "$gi_hooks_object" | awk '$2=="D" && $3 ~ /^GameInteractor_Execute/ {print $3}' | sort -u)
    # Registering a hook would insert into those unbuilt registries (null read: the
    # first pause inside a dungeon crashed in GameInteractor_RegisterOnAssetAltChange).
    while read -r gi_symbol; do
        echo "void __wrap_${gi_symbol}(void* fn);"
        echo "void __wrap_${gi_symbol}(void* fn) { (void)fn; }"
        gi_wrap_flags+=("-Wl,--wrap=${gi_symbol}")
    done < <("$NM_BIN" "$gi_hooks_object" | awk '$2=="D" && $3 ~ /^GameInteractor_Register/ {print $3}' | sort -u)
    # Per-actor "should" hooks walk the same unbuilt registries: always allow the
    # vanilla behavior, like the global GameInteractor_Should wrap.
    for gi_symbol in GameInteractor_ShouldActorInit GameInteractor_ShouldActorUpdate GameInteractor_ShouldActorDestroy; do
        echo "_Bool __wrap_${gi_symbol}(void* actor);"
        echo "_Bool __wrap_${gi_symbol}(void* actor) { (void)actor; return 1; }"
        gi_wrap_flags+=("-Wl,--wrap=${gi_symbol}")
    done
} > "$gi_noop_source"
if [ "${#gi_wrap_flags[@]}" -eq 0 ]; then
    echo 'BLOCKED: no GameInteractor_Execute hooks found to wrap'; exit 4
fi
"${cc_run[@]}" -std=gnu11 -O2 -mcpu=cell -Wall -Wextra -Werror \
    -c "$gi_noop_source" -o "$OUT/objects/ps3_gi_noop.o" >"$OUT/logs/gi-noop.log" 2>&1 || {
    echo 'BLOCKED compiling GameInteractor no-op hooks'; grep -m2 -E 'fatal error:|error:' "$OUT/logs/gi-noop.log"; exit 3; }
echo "GameInteractor executor hooks wrapped: ${#gi_wrap_flags[@]}"

if ! "$CXX_BIN" -mcpu=cell -Wl,--gc-sections -Wl,-Map,"$OUT/oot-native.map" \
    -L"$libdir" -L"$sdk_libdir" -o "$OUT/oot-native.elf" "${final_link_objects[@]}" \
    -Wl,--wrap=gcmInitBodyEx -Wl,--wrap=gfx_create_framebuffer -Wl,--wrap=gfx_texture_cache_clear -Wl,--wrap=GameInteractor_Should -Wl,--wrap=__malloc_lock -Wl,--wrap=__malloc_unlock -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc "${gi_wrap_flags[@]}" "$OUT/objects/ps3_gi_noop.o" \
    -lrsx -lgcm_sys -lio -lsysutil -laudio -lrt "$OUT/objects/ps3_ctor_mid105.o" "$PS3_PORTLIBS/lib/libz.a" \
    -llv2 -lm >"$OUT/logs/final-link.log" 2>&1; then
    echo 'FINAL LINK: BLOCKED'
    python3 - "$OUT/logs/final-link.log" "$OUT/final-undefined.txt" <<'PY'
import re
import sys

symbols = set()
for line in open(sys.argv[1], encoding="utf-8", errors="replace"):
    match = re.search(r"undefined reference to [`']([^']+)'", line)
    if match:
        symbols.add(match.group(1))
with open(sys.argv[2], "w", encoding="utf-8") as output:
    for symbol in sorted(symbols):
        output.write(symbol + "\n")
PY
    echo "True executable blockers: $(wc -l < "$OUT/final-undefined.txt")"
    echo 'First blockers:'
    if [ -s "$OUT/final-undefined.txt" ]; then
        head -n 25 "$OUT/final-undefined.txt"
    else
        grep -E 'multiple definition|cannot find|relocation truncated|collect2:|error:|failed' \
            "$OUT/logs/final-link.log" | tail -n 25 || tail -n 25 "$OUT/logs/final-link.log"
    fi
    echo 'Share only this summary. Full diagnostics stay in native-link-check/.'
    exit 5
fi

"$OBJCOPY_BIN" -j .ctors -O binary "$OUT/oot-native.elf" "$OUT/final-ctors.bin"
if grep -F 'librt.a(sbrk.o)' "$OUT/oot-native.map" >/dev/null; then
    echo 'FINAL LINK: BLOCKED (default librt sbrk.o was still selected)'
    exit 5
fi
if grep -F 'lock_internal.o' "$OUT/oot-native.map" >/dev/null; then
    echo 'FINAL LINK: BLOCKED (newlib lock_internal.o was still selected)'
    exit 5
fi
if grep -F 'librt.a(globfile.o)' "$OUT/oot-native.map" >/dev/null; then
    echo 'FINAL LINK: BLOCKED (default librt globfile.o was still selected)'
    exit 5
fi
if grep -F 'libstdc++.a(eh_alloc.o)' "$OUT/oot-native.map" >/dev/null; then
    echo 'FINAL LINK: BLOCKED (original libstdc++ eh_alloc constructor was still selected)'
    exit 5
fi
if ! strings "$OUT/oot-native.elf" | grep -F 'graph: RunFrame entered' >/dev/null; then
    echo 'FINAL LINK: BLOCKED (instrumented graph trace marker missing from final ELF)'
    exit 5
fi
if ! strings "$OUT/oot-native.elf" | grep -F 'title-draw: real O2R Nintendo 64 logo display list emitted' >/dev/null; then
    echo 'FINAL LINK: BLOCKED (instrumented title trace marker missing from final ELF)'
    exit 5
fi
if ! strings "$OUT/oot-native.elf" | grep -F 'gfx-process: completed' >/dev/null; then
    echo 'FINAL LINK: BLOCKED (instrumented gfx-process trace marker missing from final ELF)'
    exit 5
fi
if strings "$OUT/oot-native.elf" | grep -F 'gfx-process: before EnsureScreen' >/dev/null; then
    echo 'FINAL LINK: BLOCKED (stale EnsureScreen gfx-process path remains in final ELF)'
    exit 5
fi
echo 'Custom fixed PS3 heap provider selected; default sbrk constructor absent'
echo 'Native LV2-backed OoT audio/system heap provider selected'
echo 'Native LV2-backed OoT color framebuffer provider selected'
echo 'Native spin-locked OoT arena provider selected'
echo 'Constructor-free newlib lock provider selected; init_metalock absent'
echo 'Constructor-free PS3 cwd provider selected; __glob_file_init absent'
echo 'GCC exception allocator linked without emergency-pool constructor'
echo 'Priority-105 fixed-provider marker linked between librt and implicit libc'
echo 'Instrumented Graph_ThreadEntry object verified in final ELF'
echo 'Instrumented Title_Draw object verified in final ELF'
echo 'Instrumented Graph_ProcessGfxCommands object verified in final ELF'
echo "Final constructor table size: $(wc -c < "$OUT/final-ctors.bin") bytes"
echo 'FINAL LINK: OK'
echo "ELF size: $(wc -c < "$OUT/oot-native.elf") bytes"
echo 'This is the first linked native game executable; SELF/PKG packaging is next.'
echo 'Share only this summary.'
}

main 2>&1 | tee "$REPORT"
