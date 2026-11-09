#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE_ELF="$ROOT/native-link-check/oot-native.elf"
TARGET_NAME="oot_ps3_probe_v48"
TITLE="Ship of Gadeguion"
APP_VER="01.01"  # Ship of Gadeguion 1.0.1 (shown in the XMB game information)
APPID="OOTP30048"
CONTENTID="UP0001-${APPID}_00-OOTPS3RUNTIMEVM0"
TARGET_ELF="$ROOT/$TARGET_NAME.elf"
TARGET_SELF="$ROOT/$TARGET_NAME.self"
TARGET_PKG="$ROOT/$TARGET_NAME.pkg"
ALIAS_PKG="$ROOT/oot_ps3_native_probe_v48.pkg"
PKG_BUILD="$ROOT/build-real-native-pkg"

main() {
echo 'V48 isolated native Shipwright probe packaging'
# A failed compile can leave a valid-looking package from the previous build.
# Remove both output names before any freshness check can abort packaging.
rm -f "$ALIAS_PKG" "$TARGET_PKG"
for tool in ppu-strip sprxlinker make_self_npdrm make pkg sfo package_finalize; do
    command -v "$tool" >/dev/null 2>&1 || { echo "STOP: missing $tool"; exit 1; }
done
[ -f "$SOURCE_ELF" ] || {
    echo 'STOP: native-link-check/oot-native.elf is missing; run check-real-native-link.sh first.'
    exit 2
}
if ! strings "$SOURCE_ELF" | grep -F 'v48 audio-defer probe: native main wrapper entered' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'oot_ps3_v48_audio_probe_premain.txt' >/dev/null; then
    echo 'STOP: native ELF lacks the isolated audio-defer boot identity; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'graph: Play sequence v1; native fade; maximum 180 frames' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: restoring Hyrule Field Scene_Draw before room lists' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'scene-draw: upstream graphics segment setup completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'trace: compact Play sequence logging active after full first frame' >/dev/null; then
    echo 'STOP: native ELF lacks the scene graphics setup and bounded Play sequence; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'room-mesh: selected O2R mesh installed for upstream Room_Draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before upstream Room_Draw of decoded O2R mesh' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'room-draw: before handler dispatch' >/dev/null; then
    echo 'STOP: native ELF lacks the decoded O2R room draw; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'fast3d-run: unsupported opcode; presenting partial frame' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'resource-dlist: large list decoded' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: holding submitted Play framebuffer for three seconds' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded partial room frame renderer; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F '/dev_usb000/oot_ps3_v48_audio_probe_boot.txt' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d: translucent title fade composited' >/dev/null; then
    echo 'STOP: native ELF lacks the USB boot trace and translucent title fade; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'graph: render thread started (draws in parallel)' >/dev/null; then
    echo 'STOP: native ELF does not contain the render-thread marker.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'gfx-process: before native Fast3D display-list draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d: before real display-list execution' >/dev/null; then
    echo 'STOP: native ELF lacks the PS3 Fast3D display-list renderer; refusing stale ELF.'
    exit 2
fi
if strings "$SOURCE_ELF" | grep -F 'gfx-process: before EnsureScreen' >/dev/null; then
    echo 'STOP: native ELF still contains the stale EnsureScreen path; refusing to package it.'
    exit 2
fi

if ! strings "$SOURCE_ELF" | grep -F 'gcm-init: before resolved gcmInitBodyEx' >/dev/null; then
    echo 'STOP: native ELF lacks the current resolved-RSX startup probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'rsx-init: aligned host allocation ready' >/dev/null; then
    echo 'STOP: native ELF lacks the aligned-host RSX probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'gfx-dl-probe: extended title opcode scan' >/dev/null; then
    echo 'STOP: native ELF lacks the extended display-list probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'gfx-process: title geometry preview submitted' >/dev/null; then
    echo 'STOP: native ELF lacks the title geometry preview; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'gfx-process: archive title texture presented' >/dev/null; then
    echo 'STOP: native ELF lacks the archive title texture path; refusing stale ELF.'
    exit 2
fi
# (Title-stub markers are compiled out and their checks below read 'true': the title intro uses the general scene loader,
#  OOT_PS3_USE_TITLE_STUB 0, so the compiler drops the stub's trace strings.)
if ! strings "$SOURCE_ELF" | grep -F 'actor-init: before Actor_SpawnEntry' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-spawn-entry: PS3 diagnostic uses default allow-spawn result' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-init-instance: PS3 diagnostic uses default actor-init hook result' >/dev/null; then
    echo 'STOP: native ELF lacks fresh upstream actor initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'actor-update-all: before genuine Epona update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: before slippery-floor hook' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-scene-collision: deferred for the title stub' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: PS3 uses default player action result' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: completed' >/dev/null; then
    echo 'STOP: native ELF lacks fresh actor/player update probes; refusing stale ELF.'
    exit 2
fi
if ! true ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-update-all: after scene actor list' >/dev/null; then
    echo 'STOP: native ELF lacks initialized empty scene actor list; refusing stale ELF.'
    exit 2
fi
if ! true ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-db: actor entries registered' >/dev/null; then
    echo 'STOP: native ELF lacks constructor-safe ActorDB initialization; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'actor-spawn: PS3 diagnostic skips optional actor-list index' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-spawn: instance allocation failed' >/dev/null; then
    echo 'STOP: native ELF lacks the current actor-allocation probe; refusing stale ELF.'
    exit 2
fi
if ! true ||
   ! strings "$SOURCE_ELF" | grep -F 'play-init-final: before Interface_SetSceneRestrictions' >/dev/null; then
    echo 'STOP: native ELF lacks current Play camera-defer probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-init: before Camera_InitPlayerSettings' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-init: before scene collision header lookup' >/dev/null; then
    echo 'STOP: native ELF lacks fresh Play camera/collision probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-init: invoking start mode' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-start-movement: scene collision absent; using ground start' >/dev/null; then
    echo 'STOP: native ELF lacks current player start-mode probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-init: before Player_UseItem' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-init-common: before first SkelAnime_InitLink' >/dev/null; then
    echo 'STOP: native ELF lacks fresh player initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-item-action: PS3 diagnostic uses default no-item check' >/dev/null; then
    echo 'STOP: native ELF lacks the constructor-safe player item-action path; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-init-common: before Actor_ProcessInitChain' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-init-audio: PS3 diagnostic defers stop-SFX calls' >/dev/null; then
    echo 'STOP: native ELF lacks the current player-common audio probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'resource: native skeleton and limb table decoded' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'resource: native Link animation decoded' >/dev/null; then
    echo 'STOP: native ELF lacks fresh skeleton and Link animation importers; refusing stale ELF.'
    exit 1
fi

if ! strings "$SOURCE_ELF" | grep -F 'skel-init-link: before archive skeleton load' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'link-animation-change: before archive animation load' >/dev/null; then
    echo 'STOP: native ELF lacks fresh skeleton and animation probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'resource: title texture entry read succeeded' >/dev/null; then
    echo 'STOP: native ELF lacks the current 64-byte O2R resource reader; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'resource-index: oot.o2r ZIP end record found' >/dev/null; then
    echo 'STOP: native ELF lacks the O2R archive indexer trace; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'graph: title frame 36 complete without Opening handoff; exiting to XMB' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-stats: triangles=%u valid=%u pixels=%u uploads=%u size=%ux%u' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-output: nonblack=%u colored-writes=%u textured-writes=%u clears=%u center=%08x' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d: native offscreen game framebuffer presented' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d: native full-screen fade fast path completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d: reduced internal raster, full PS3 output mode' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded Fast3D title pixel probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'fast3d-rsx: drawing upstream game triangles on RSX' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-combine: rgb0=' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-rsx: title TEXEL1 texture path submitted' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-rsx: actual Shipwright frame presented' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-rsx: shader programs and vertex buffers ready' >/dev/null; then
    echo 'STOP: native ELF lacks the fresh game display-list RSX renderer; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'intro-route: real Hyrule Field title entrance selected' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: selected O2R room=' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: scene layer %u archive header=%s' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: title cutscene O2R type=' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: title HUD icons deferred; continuing Play_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: title minimap load deferred; continuing Play_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-route: title HUD update deferred; scene update continues' >/dev/null; then
    echo 'STOP: native ELF lacks the Hyrule Field title entrance and layered scene probe.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'intro-horse: object lookup actor=%d object=%d bank-count=%d' >/dev/null ||
   ! true ||
   ! true; then
    echo 'STOP: native ELF lacks the bounded title horse object bank lookup.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'trace: persistent HDD handles and USB mirror active' >/dev/null; then
    echo 'STOP: native ELF lacks the reduced trace I/O for RSX frame progression.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'trace: compact title-frame mode active after full frame-one trace' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded title-frame log reduction.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-update: entered' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-update: PS3 defers Environment_Update until scene commands load' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-update: before cutscene update 64558' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'cutscene-update: PS3 skips developer debug shortcuts' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'cutscene-start: before Flags_UnsetEnv' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'cutscene-commands: scene script missing; deferred' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'cutscene-parser: O2R script unavailable; deferred' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic real Play frame Graph_Update completed; before RSX draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic bounded Play sequence completed; exiting to XMB' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'title-main: native fade completed; Opening selected' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'title-init: accelerated native fade for Opening handoff' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'title-calc: fade-in completed; fast fade-out armed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'title-calc: fade-out completed; natural Opening handoff ready' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'game-update: before state main' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before Scene_Draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before main keep segment conversion' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before scene segment commands' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before opaque scene segment command' >/dev/null ||
   ! true ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: scene and room draw deferred until O2R headers load' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: before player Actor_Draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw: before player draw callback' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw: render setup completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw: before real Link limb draw' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: before real skeleton traversal' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: default game tunic color applied' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-limb-common: entered with default game equipment settings' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-limb-default: native game limb model selected' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-limb-ik: scene collision missing; original animated leg pose kept' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-limb-post: right hand attachment effects completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-strength: native save upgrade and age requirement used' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-strength: before saved upgrade lookup' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: native equipment conditions passed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: adult strength upgrade decoded' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: optional boot overlay deferred without scene data' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'title-draw: real O2R Nintendo 64 logo display list emitted' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'logo-3d: bounded O2R display list decoded' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw-impl: game equipment pass completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'trace: compact Link limb success markers active' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'skel-draw: entered real Link flex skeleton' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'skel-draw: flex native game LOD selected without PC enhancement lookup' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'skel-draw: invalid limb index or cyclic traversal; stopped safely' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-draw: completed' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: scene segment commands emitted' >/dev/null; then
    echo 'STOP: native ELF lacks current Play_Draw segment probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic Play_Init completed; before first real Graph_Update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: Opening still running after one frame; exiting to XMB' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded post-Opening probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'game-init: before state init callback' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-init: before Play_SpawnScene' >/dev/null; then
    echo 'STOP: native ELF lacks fresh Play and game-state initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-init: before Message_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-init: entrance lookup completed' >/dev/null; then
    echo 'STOP: native ELF lacks detailed Play initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'cutscene-conditional: PS3 diagnostic skips uninitialized hook' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'cutscene-entrance: PS3 diagnostic skips uninitialized hook' >/dev/null; then
    echo 'STOP: native ELF lacks the fresh cutscene conditional probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'message-init: NTSC font table unavailable; diagnostic skip' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'interface-init: before parameter DMA' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'message-tables: before OTRMessage_Init' >/dev/null; then
    echo 'STOP: native ELF lacks fresh internal message initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic release Title arena; enter Opening_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic state arena released' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'game-init-arena: allocation FAILED' >/dev/null; then
    echo 'STOP: native ELF lacks fresh state-arena release and allocation probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'rsx-watchdog: no frame for %u s (no trace for %u s); stopped at: %s' >/dev/null; then
    echo 'STOP: native ELF lacks the frame-stall watchdog; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'intro-horse-init: before real horse Skin_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-init: completed genuine EnHorse_Init' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-skin: genuine skinned animation initialized' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'resource: native horse skin limb decoded' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'resource: native horse normal animation decoded' >/dev/null; then
    echo 'STOP: native ELF lacks fresh real horse actor initialization probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-init-final: activating Play budget before genuine horse spawn' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-setup: after horse camera request' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-setup: Horse_InitPlayerHorse completed' >/dev/null; then
    echo 'STOP: native ELF lacks early bounded Play budget or genuine Epona mount probes; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-camera: scene commands absent; deferring horse camera setting' >/dev/null; then
    echo 'STOP: native ELF lacks the room-safe genuine horse camera probe; refusing stale ELF.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'intro-horse-camera: room not loaded; deferring horse camera request' >/dev/null; then
    echo 'STOP: native ELF lacks the unloaded-room guard in the genuine horse camera helper.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'actor-update-all: before genuine Epona update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-update: entered genuine EnHorse_Update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-update: floor queries deferred until scene collision header loads' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'intro-horse-update: completed genuine EnHorse_Update' >/dev/null; then
    echo 'STOP: native ELF lacks the genuine Epona first-frame update and missing-scene guard.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-update-common: mounted animation movement branch entered' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: mounted floor type uses default hook result' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: mounted floor raycast deferred until scene collision loads' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-update-common: mounted animation movement branch completed' >/dev/null; then
    echo 'STOP: native ELF lacks the mounted Link floor guard in the genuine first Play frame.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: entered genuine horseback action' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: before limb animation copy' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: after real upper-body update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: completed genuine horseback action' >/dev/null; then
    echo 'STOP: native ELF lacks the genuine mounted player action frontier probe.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-upper: entered genuine upper-body update' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-upper: default hookshot-parent result used' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-upper-items: default quick-putaway result used' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-upper: before genuine upper action callback' >/dev/null; then
    echo 'STOP: native ELF lacks the real mounted upper-body update hook defaults.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: unloaded-room talk and roll deferred' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'player-horse-action: mounted world interaction check completed' >/dev/null; then
    echo 'STOP: native ELF lacks the unloaded-room mounted interaction guard.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: scene-dependent effects deferred until room loads' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: collision overlay deferred until scene loads' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: before closing actor display lists' >/dev/null; then
    echo 'STOP: native ELF lacks the real first Play draw scene-dependent effects guard.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: unloaded scene glow lights deferred' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'actor-draw-all: unloaded scene title card deferred' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'graph: diagnostic bounded Play sequence completed; exiting to XMB' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded real Play frame glow-light guard.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'play-draw: scene-dependent environment and overlays deferred; preserving real actor display lists' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: before closing frame display lists' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'play-draw: gameplay timer deferred until first scene loads' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded first Play frame actor-only display-list completion.'
    exit 2
fi
if ! strings "$SOURCE_ELF" | grep -F 'fast3d-run: after backend ClearFramebuffer' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-run: display-list loop entered' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-run: command loop returned; before Flush' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-run: malformed display-list command budget reached' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-texture: invalid lower 32 bits before SegAddr full=%016llx' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-texture: deferred palette load after invalid texture address' >/dev/null ||
   ! strings "$SOURCE_ELF" | grep -F 'fast3d-dlist: deferred invalid Play display-list pointer' >/dev/null; then
    echo 'STOP: native ELF lacks the bounded genuine Fast3D Play command probe.'
    exit 2
fi

# PSL1GHT's own ppu_rules strips and links SPRX imports *before* signing.
# Keep the original linked ELF untouched for source marker checks and debugging.
cp "$SOURCE_ELF" "$TARGET_ELF"
echo "Input ELF: $(wc -c < "$TARGET_ELF") bytes"
ppu-strip "$TARGET_ELF"
sprxlinker "$TARGET_ELF"

# An unprocessed import record advertises zero functions.  Such a SELF can
# boot using LV2 syscalls yet loops in the first dynamic GCM import.
python3 - "$TARGET_ELF" <<'PYVALIDATE'
import struct
import sys

content = open(sys.argv[1], 'rb').read()
if content[:6] != b'\x7fELF\x02\x02':
    raise SystemExit('STOP: processed binary is not big-endian ELF64')
phoff = struct.unpack_from('>Q', content, 32)[0]
phsize, phnum = struct.unpack_from('>HH', content, 54)
params = None
loads = []
for i in range(phnum):
    ty, flags, off, addr, paddr, size, memsize, align = struct.unpack_from(
        '>IIQQQQQQ', content, phoff + i * phsize)
    if ty == 1:
        loads.append((addr, addr + size, off))
    if ty == 0x60000002:
        params = off
if params is None or struct.unpack_from('>I', content, params + 4)[0] != 0x1B434CEC:
    raise SystemExit('STOP: processed ELF has no PS3 import metadata')
start, end = struct.unpack_from('>II', content, params + 24)
def file_offset(address):
    for lower, upper, off in loads:
        if lower <= address < upper:
            return off + address - lower
    raise SystemExit('STOP: import record outside loadable ELF segments')
if start >= end:
    raise SystemExit('STOP: processed ELF has empty import table')
addr = start
counts = []
while addr < end:
    off = file_offset(addr)
    length = content[off]
    if length < 0x2C or addr + length > end:
        raise SystemExit('STOP: malformed processed PS3 import record')
    count = struct.unpack_from('>H', content, off + 6)[0]
    if count == 0:
        raise SystemExit('STOP: sprxlinker left a zero-function import record')
    counts.append(count)
    addr += length
if addr != end or len(counts) < 4:
    raise SystemExit('STOP: unexpected PS3 import-table layout')
print('PS3 imports linked; functions by library:', ', '.join(map(str, counts)))
PYVALIDATE

# Produce the NPDRM SELF explicitly.  The isolated make invocation below has
# no source files, so it can package this exact linked executable without ever
# invoking the old synthetic-demo linker.
make_self_npdrm "$TARGET_ELF" "$TARGET_SELF" "$CONTENTID"
echo "NPDRM SELF: $(wc -c < "$TARGET_SELF") bytes"

rm -rf "$PKG_BUILD"
mkdir -p "$PKG_BUILD/pkg/USRDIR"
cp "${PS3DEV:-/usr/local/ps3dev}/bin/ICON0.PNG" "$PKG_BUILD/pkg/ICON0.PNG"
make_self_npdrm "$TARGET_ELF" "$PKG_BUILD/pkg/USRDIR/EBOOT.BIN" "$CONTENTID" >/dev/null
python3 - "${PS3DEV:-/usr/local/ps3dev}/bin/sfo.xml" "$PKG_BUILD/sfo.xml" "$APP_VER" <<'PY'
import re, sys
xml = open(sys.argv[1]).read()
for key in ("APP_VER", "VERSION"):
    xml, n = re.subn(r'(<value name="%s" type="string">\s*)[0-9.]+' % key, r"\g<1>" + sys.argv[3], xml)
    assert n == 1, key
open(sys.argv[2], "w").write(xml)
PY
sfo --title "$TITLE" --appid "$APPID" -f "$PKG_BUILD/sfo.xml" "$PKG_BUILD/pkg/PARAM.SFO"
pkg --contentid "$CONTENTID" "$PKG_BUILD/pkg/" "$TARGET_PKG"

[ -f "$TARGET_PKG" ] || { echo 'STOP: packaging completed without producing the expected PKG.'; exit 3; }
cp "$TARGET_PKG" "$ALIAS_PKG"
# Keep the OoT PS3 Packager in step with every build: its payload (EBOOT.BIN, PARAM.SFO)
# and sources (build_exe.bat makes the .exe from tools/packager or a synced copy).
cp "$PKG_BUILD/pkg/USRDIR/EBOOT.BIN" "$PKG_BUILD/pkg/PARAM.SFO" "$ROOT/tools/packager/payload/"
# Optional: OOT_PS3_PACKAGER_SYNC_DIR=<folder with payload/> mirrors it there too.
WIN_PACKAGER="${OOT_PS3_PACKAGER_SYNC_DIR:-}"
if [ -n "$WIN_PACKAGER" ] && [ -d "$WIN_PACKAGER/payload" ]; then
  cp "$ROOT/tools/packager/payload/"* "$WIN_PACKAGER/payload/"
  cp "$ROOT/tools/packager/"*.py "$WIN_PACKAGER/"
fi
echo 'NATIVE PKG: OK'
echo "PKG path: $ALIAS_PKG"
echo "PKG size: $(wc -c < "$ALIAS_PKG") bytes"
echo 'No Nintendo ROM, OTR, or O2R data is included.'
}

main
