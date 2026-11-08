#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
UPSTREAM="$ROOT/upstream/shipwright"
SOURCE="$UPSTREAM/libultraship/src/fast/interpreter.cpp"
OUT="$ROOT/native-link-check/logs/fast3d-interpreter-compile.log"
OBJECT="$ROOT/native-link-check/objects/fast3d-interpreter.o"
RESOURCE_OBJECT="$ROOT/native-link-check/objects/fast3d-resource-ps3.o"
DEBUGGER_OBJECT="$ROOT/native-link-check/objects/fast3d-debugger.o"
VIDEO_OBJECT="$ROOT/native-link-check/objects/fast3d-video-ps3.o"
CXX_BIN="${OOT_PS3_CXX:-ppu-g++}"
PS3_SDK_ROOT="${PSL1GHT:-${PS3DEV:-/usr/local/ps3dev}}"

command -v "$CXX_BIN" >/dev/null 2>&1 || { echo "STOP: missing $CXX_BIN"; exit 1; }
[ -f "$SOURCE" ] || { echo 'STOP: Shipwright Fast3D interpreter source is missing'; exit 1; }
mkdir -p "$(dirname "$OUT")"

# The Fast3D interpreter never calls the PC GUI. Its direct Gui.h include
# pulls in ImGui/ConsoleWindow types that the PS3 build intentionally lacks.
# Remove just that include in a temporary copy, leaving upstream untouched.
PROBE_SOURCE="$(mktemp "$ROOT/native-link-check/fast3d-ps3-probe-XXXXXX.cpp")"
trap 'rm -f "$PROBE_SOURCE"' EXIT
python3 - "$SOURCE" "$PROBE_SOURCE" <<'PY'
import pathlib
import sys

source = pathlib.Path(sys.argv[1]).read_bytes()
gui_include = b'#include "ship/window/gui/Gui.h"\n'
if source.count(gui_include) != 1:
    raise SystemExit('STOP: expected exactly one direct PC GUI include in the upstream interpreter')
source = source.replace(gui_include, b'', 1)

# PS3 has no dladdr or dynamically loaded module map. An address left in the
# N64 segmented range cannot safely be dereferenced during this diagnostic.
desktop_guard = (b'#ifdef _WIN32\n'
                 b'    // For Windows, check whether the address belongs to a dll.\n')
ps3_guard = (b'#ifdef __PS3__\n'
             b'    return false;\n'
             b'#elif defined(_WIN32)\n'
             b'    // For Windows, check whether the address belongs to a dll.\n')
if source.count(desktop_guard) != 1:
    raise SystemExit('STOP: upstream low-address module guard changed')
source = source.replace(desktop_guard, ps3_guard, 1)

# std::fill and std::pair assignment are not constexpr in this PS3 C++17
# standard library. Initialize the fixed table to zero and assign its scalar
# fields at compile time. Keep the existing static constexpr opcode tables:
# runtime construction is unsafe because the PS3 bootstrap skips most ctors.
old_table_init = (b'        std::fill(std::begin(mHandlers), std::end(mHandlers),\n'
                  b'                  std::pair<const char*, GfxOpcodeHandlerFunc>(nullptr, nullptr));\n\n'
                  b'        for (const auto& [opcode, handler] : initializer) {\n'
                  b'            mHandlers[static_cast<uint8_t>(opcode)] = handler;\n'
                  b'        }')
new_table_init = (b'        for (const auto& [opcode, handler] : initializer) {\n'
                  b'            mHandlers[static_cast<uint8_t>(opcode)].first = handler.first;\n'
                  b'            mHandlers[static_cast<uint8_t>(opcode)].second = handler.second;\n'
                  b'        }')
old_ctor = (b'        std::initializer_list<std::pair<int8_t, std::pair<const char*, GfxOpcodeHandlerFunc>>> initializer) {')
new_ctor = (b'        std::initializer_list<std::pair<int8_t, std::pair<const char*, GfxOpcodeHandlerFunc>>> initializer)\n'
            b'        : mHandlers{} {')
if source.count(old_table_init) != 1 or source.count(old_ctor) != 1:
    raise SystemExit('STOP: upstream constexpr opcode table changed')
source = source.replace(old_table_init, new_table_init, 1).replace(old_ctor, new_ctor, 1)

# The interpreter previously obtained SPDLOG macros transitively through
# desktop Context headers. Keep PS3 errors in the existing boot trace.
fmt_include = b'#include <spdlog/fmt/fmt.h>\n'
if source.count(fmt_include) != 1:
    raise SystemExit('STOP: upstream logging include changed')
source = source.replace(fmt_include,
                        fmt_include +
                        b'extern "C" void oot_ps3_early_trace(const char*);\n'
                        b'extern "C" void oot_ps3_trace_resource_address(const void*);\n'
                        b'#define SPDLOG_ERROR(message, ...) oot_ps3_early_trace(message)\n'
                        b'#define SPDLOG_WARN(message, ...) oot_ps3_early_trace(message)\n'
                        b'#define SPDLOG_CRITICAL(message, ...) oot_ps3_early_trace(message)\n'
                        b'#define SPDLOG_INFO(message, ...) oot_ps3_early_trace(message)\n', 1)

# The desktop CVar names are not provided by the PS3 bootstrap. Its native
# framebuffer is already fixed to one-to-one resolution without MSAA.
desktop_resolution = (b'    mCurDimensions.internal_mul =\n'
                      b'        Ship::Context::GetRawInstance()->GetConsoleVariables()->GetFloat(CVAR_INTERNAL_RESOLUTION, 1);\n'
                      b'    mMsaaLevel = Ship::Context::GetRawInstance()->GetConsoleVariables()->GetInteger(CVAR_MSAA_VALUE, 1);')
ps3_resolution = b'    mCurDimensions.internal_mul = 1.0f;\n    mMsaaLevel = 1;'
if source.count(desktop_resolution) != 1:
    raise SystemExit('STOP: upstream renderer resolution settings changed')
source = source.replace(desktop_resolution, ps3_resolution, 1)

# The PS3 archive bridge resolves actual O2R contents synchronously. Route
# desktop Context resource calls to a typed facade without constructing the
# desktop Context (which requires an unsupported window/GUI and constructors).
context_resource = b'Ship::Context::GetRawInstance()->GetResourceManager()'
if source.count(context_resource) < 10:
    raise SystemExit('STOP: upstream Fast3D resource call pattern changed')
source = source.replace(context_resource, b'Ps3ResourceManager()')
for desktop_include in (
    b'#include "ship/resource/ResourceManager.h"\n',
    b'#include "ship/utils/Utils.h"\n',
    b'#include "ship/Context.h"\n',
    b'#include "ship/config/ConsoleVariable.h"\n',
):
    if source.count(desktop_include) != 1:
        raise SystemExit('STOP: upstream desktop resource/context includes changed')
    source = source.replace(desktop_include, b'', 1)
interpreter_include = b'#include "fast/interpreter.h"\n'
if source.count(interpreter_include) != 1:
    raise SystemExit('STOP: upstream Fast3D interpreter include changed')
source = source.replace(interpreter_include,
                        interpreter_include + b'#include "ps3_platform/fast3d_resource_ps3.h"\n', 1)

# The first genuine Play display list contains an uninitialized texture
# address, 0xABABABAB. Fast3D's signature check dereferences it before the
# renderer can reach any game triangles. Defer this one texture assignment,
# report it, and continue interpreting the game's own commands.
texture_entry = (b'    uintptr_t i = (uintptr_t)gfx->SegAddr(cmd->words.w1);\n'
                 b'\n'
                 b'    char* imgData = (char*)i;')
texture_guard = (b'    if ((uint32_t)cmd->words.w1 == 0xABABABABu) {\n'
                 b'        gfx->mRdp->texture_to_load.addr = nullptr;\n'
                 b'        char ps3BadTexture[128];\n'
                 b'        snprintf(ps3BadTexture, sizeof(ps3BadTexture),\n'
                 b'                 "fast3d-texture: invalid lower 32 bits before SegAddr full=%016llx",\n'
                 b'                 (unsigned long long)cmd->words.w1);\n'
                 b'        oot_ps3_early_trace(ps3BadTexture);\n'
                 b'        return false;\n'
                 b'    }\n'
                 b'    uintptr_t i = (uintptr_t)gfx->SegAddr(cmd->words.w1);\n'
                 b'\n'
                 b'    char* imgData = (char*)i;')
if source.count(texture_entry) != 1:
    raise SystemExit('STOP: upstream Fast3D raw texture handler changed')
source = source.replace(texture_entry, texture_guard, 1)

# A rejected texture must not leave the preceding title texture selected.
# In the mounted Play frame the immediately following G_LOADTLUT would copy
# palette bytes from that stale pointer. Defer the dependent palette and tile
# loads until a genuine subsequent G_SETTIMG supplies an address.
for function_name, probe_name in (
    (b'GfxDpLoadTlut(uint8_t tile, uint32_t high_index)', b'palette'),
    (b'GfxDpLoadBlock(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t dxt)', b'block'),
    (b'GfxDpLoadTile(uint8_t tile, uint32_t uls, uint32_t ult, uint32_t lrs, uint32_t lrt)', b'tile'),
):
    entry = b'void Interpreter::' + function_name + b' {\n'
    if source.count(entry) != 1:
        raise SystemExit('STOP: upstream Fast3D ' + probe_name.decode() + ' load changed')
    guard = (b'    if (!mRdp->texture_to_load.addr) {\n'
             b'        oot_ps3_early_trace("fast3d-texture: deferred ' + probe_name +
             b' load after invalid texture address");\n'
             b'        return;\n'
             b'    }\n')
    source = source.replace(entry, entry + guard, 1)

# Invalid Play resources also surface as direct G_DL calls. The 64-bit fill
# pattern must be rejected before SegAddr indexes its small segment table.
dl_entry = (b'bool gfx_dl_handler_common(F3DGfx** cmd0) {\n'
            b'    Interpreter* gfx = mInstance.lock().get();\n'
            b'    F3DGfx* cmd = *cmd0;\n'
            b'    F3DGfx* subGFX = (F3DGfx*)gfx->SegAddr(cmd->words.w1);')
dl_guard = (b'bool gfx_dl_handler_common(F3DGfx** cmd0) {\n'
            b'    Interpreter* gfx = mInstance.lock().get();\n'
            b'    F3DGfx* cmd = *cmd0;\n'
            b'    if ((uint32_t)cmd->words.w1 == 0xABABABABu) {\n'
            b'        oot_ps3_early_trace("fast3d-dlist: deferred invalid Play display-list pointer");\n'
            b'        if (C0(16, 1) != 0) { g_exec_stack.ret(); return true; }\n'
            b'        return false;\n'
            b'    }\n'
            b'    F3DGfx* subGFX = (F3DGfx*)gfx->SegAddr(cmd->words.w1);\n'
            b'    const uintptr_t ps3Target = reinterpret_cast<uintptr_t>(subGFX);\n'
            b'    if (ps3Target <= 0x0FFFFFFFu || ps3Target > 0xFFFFFFFFull || (ps3Target & 7u)) {\n'
            b'        char ps3BadDl[144];\n'
            b'        snprintf(ps3BadDl, sizeof(ps3BadDl), "fast3d-dlist: unresolved target raw=%016llx resolved=%016llx caller=%p",\n'
            b'                 (unsigned long long)cmd->words.w1, (unsigned long long)ps3Target, (void*)cmd);\n'
            b'        oot_ps3_early_trace(ps3BadDl);\n'
            b'        if (C0(16, 1) != 0) { g_exec_stack.ret(); return true; }\n'
            b'        return false;\n'
            b'    }')
if source.count(dl_entry) != 1:
    raise SystemExit('STOP: upstream Fast3D display-list call changed')
source = source.replace(dl_entry, dl_guard, 1)
seg_entry = (b'        uint32_t segNum = (uint32_t)(w1 >> 24);\n'
             b'\n'
             b'        uint32_t offset = w1 & 0x00FFFFFE;')
seg_guard = (b'        uint32_t segNum = (uint32_t)(w1 >> 24);\n'
             b'        if (segNum >= MAX_SEGMENT_POINTERS) {\n'
             b'            oot_ps3_early_trace("fast3d-segment: invalid index rejected");\n'
             b'            return nullptr;\n'
             b'        }\n'
             b'\n'
             b'        uint32_t offset = w1 & 0x00FFFFFE;')
if source.count(seg_entry) != 1:
    raise SystemExit('STOP: upstream Fast3D segmented pointer handling changed')
source = source.replace(seg_entry, seg_guard, 1)

# The existing PS3 link removes normal project pre-main initializers. Make
# Fast3D's three mutable namespace globals initialize on first runtime use.
globals = {
    b'std::stack<std::string> currentDir;':
        b'static std::stack<std::string>& Ps3CurrentDir() {\n'
        b'    static std::stack<std::string> value;\n    return value;\n}\n'
        b'#define currentDir Ps3CurrentDir()',
    b'static std::weak_ptr<Interpreter> mInstance;':
        b'static std::weak_ptr<Interpreter>& Ps3Instance() {\n'
        b'    static std::weak_ptr<Interpreter> value;\n    return value;\n}\n'
        b'#define mInstance Ps3Instance()',
    b'GfxExecStack g_exec_stack = {};':
        b'static GfxExecStack& Ps3ExecStack() {\n'
        b'    static GfxExecStack value{};\n    return value;\n}',
}
for original, replacement in globals.items():
    if source.count(original) != 1:
        raise SystemExit('STOP: upstream Fast3D global layout changed')
    source = source.replace(original, replacement, 1)
first_namespace = b'namespace Fast {\n'
if source.count(first_namespace) != 1:
    raise SystemExit('STOP: upstream Fast3D namespace layout changed')
source = source.replace(first_namespace,
                        first_namespace +
                        b'static GfxExecStack& Ps3ExecStack();\n'
                        b'#define g_exec_stack Ps3ExecStack()\n', 1)
# GameState_Update requests the pause preview framebuffer before the first
# Graph_ProcessGfxCommands initializes the interpreter. Leave its sentinel
# (-1) untouched until Fast3D is ready; the next update retries the request.
framebuffer_call = b'    return Fast::mInstance.lock().get()->CreateFrameBuffer(width, height, native_width, native_height, resize,\n'
if source.count(framebuffer_call) != 1:
    raise SystemExit('STOP: upstream Fast3D framebuffer entry changed')
source = source.replace(framebuffer_call,
                        b'    if (!Fast::mInstance.lock()) return -1;\n' + framebuffer_call, 1)
texture_clear_call = b'    Fast::mInstance.lock().get()->TextureCacheClear();\n'
if source.count(texture_clear_call) != 1:
    raise SystemExit('STOP: upstream Fast3D texture cache entry changed')
source = source.replace(texture_clear_call,
                        b'    if (!Fast::mInstance.lock()) return;\n' + texture_clear_call, 1)
# Graph's PS3 diagnostics never install the desktop gfx debugger.
if source.count(b'if (dbg->IsDebugging()) {') != 1:
    raise SystemExit('STOP: upstream Fast3D debugger branch changed')
source = source.replace(b'if (dbg->IsDebugging()) {', b'if (dbg && dbg->IsDebugging()) {', 1)

# Trace the real game command stream at the interpreter boundary. The title
# frame already works; this identifies whether the Play frame blocks during
# framebuffer setup or inside a particular display-list opcode. The bounded
# command count prevents a malformed branch from looping forever.
run_begin = (b'    mRapi->StartFrame();\n'
             b'    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, (float)mCurDimensions.height / mNativeDimensions.height);\n'
             b'    mRapi->ClearFramebuffer(true, true);')
run_trace = (b'    oot_ps3_early_trace("fast3d-run: before backend StartFrame");\n'
             b'    mRapi->StartFrame();\n'
             b'    oot_ps3_early_trace("fast3d-run: after backend StartFrame");\n'
             b'    mRapi->StartDrawToFramebuffer(mRendersToFb ? mGameFb : 0, (float)mCurDimensions.height / mNativeDimensions.height);\n'
             b'    oot_ps3_early_trace("fast3d-run: before backend ClearFramebuffer");\n'
             b'    mRapi->ClearFramebuffer(true, true);\n'
             b'    oot_ps3_early_trace("fast3d-run: after backend ClearFramebuffer");')
run_signature = b'void Interpreter::Run(Gfx* commands, const std::unordered_map<Mtx*, MtxF>& mtx_replacements) {'
if source.count(run_signature) != 1:
    raise SystemExit('STOP: upstream Fast3D Run entry changed')
run_start = source.index(run_signature)
run_source = source[run_start:]
run_entry = run_signature + b'\n    SpReset();'
if run_source.count(run_entry) != 1:
    raise SystemExit('STOP: upstream Fast3D Run state reset changed')
run_source = run_source.replace(run_entry,
    run_signature +
    b'\n    oot_ps3_early_trace("fast3d-run: entered interpreter Run");'
    b'\n    SpReset();'
    b'\n    oot_ps3_early_trace("fast3d-run: after SpReset");', 1)
fb_update = (b'    mRapi->UpdateFramebufferParameters(0, mGfxCurrentWindowDimensions.width, mGfxCurrentWindowDimensions.height, 1,\n'
             b'                                       false, true, true, !mRendersToFb);')
if run_source.count(fb_update) != 1:
    raise SystemExit('STOP: upstream Fast3D Run framebuffer parameters changed')
run_source = run_source.replace(fb_update,
    b'    oot_ps3_early_trace("fast3d-run: before framebuffer parameter update");\n' +
    fb_update +
    b'\n    oot_ps3_early_trace("fast3d-run: after framebuffer parameter update");', 1)
if run_source.count(run_begin) != 1:
    raise SystemExit('STOP: upstream Fast3D Run framebuffer setup changed')
run_source = run_source.replace(run_begin, run_trace, 1)
run_loop = (b'    g_exec_stack.start((F3DGfx*)commands);\n'
            b'    while (!g_exec_stack.cmd_stack.empty()) {\n'
            b'        auto cmd = g_exec_stack.cmd_stack.top();')
loop_trace = (b'    g_exec_stack.start((F3DGfx*)commands);\n'
              b'    oot_ps3_early_trace("fast3d-run: display-list loop entered");\n'
              b'    unsigned ps3StepCount = 0;\n'
              b'    while (!g_exec_stack.cmd_stack.empty()) {\n'
              b'        if (++ps3StepCount > 131072) {\n'
              b'            oot_ps3_early_trace("fast3d-run: malformed display-list command budget reached");\n'
              b'            break;\n'
              b'        }\n'
              b'        auto cmd = g_exec_stack.cmd_stack.top();\n'
              b'        if (!cmd) {\n'
              b'            oot_ps3_early_trace("fast3d-run: null command; presenting partial frame");\n'
              b'            g_exec_stack.stop();\n'
              b'            break;\n'
              b'        }\n'
              b'        const int8_t ps3Opcode = (int8_t)(cmd->words.w0 >> 24);\n'
              b'        if (ps3TimeOps) {\n'
              b'            unsigned long long ps3OpcodeNowUs = oot_ps3_microseconds_now();\n'
              b'            if (ps3PrevOpcodeValid) {\n'
              b'                ps3OpcodeTimeUs[ps3PrevOpcode] += ps3OpcodeNowUs - ps3OpcodeTimerUs;\n'
              b'                ps3OpcodeCount[ps3PrevOpcode] += 1;\n'
              b'            }\n'
              b'            ps3PrevOpcode = (uint8_t)ps3Opcode;\n'
              b'            ps3PrevOpcodeValid = 1;\n'
              b'            ps3OpcodeTimerUs = ps3OpcodeNowUs;\n'
              b'        }\n'
              b'        const bool ps3Supported = ps3Opcode == F3DEX2_G_LOAD_UCODE ||\n'
              b'            otrHandlers.contains(ps3Opcode) || rdpHandlers.contains(ps3Opcode) ||\n'
              b'            (ucode_handler_index < ucode_handlers.size() &&\n'
              b'             ucode_handlers[ucode_handler_index]->contains(ps3Opcode));\n'
              b'        if (!ps3Supported) {\n'
              b'            char ps3BadCommand[192];\n'
              b'            snprintf(ps3BadCommand, sizeof(ps3BadCommand),\n'
              b'                     "fast3d-run: unsupported opcode=%02x ucode=%u step=%u cmd=%p w0=%08x w1=%016llx",\n'
              b'                     (unsigned)(uint8_t)ps3Opcode, (unsigned)ucode_handler_index, ps3StepCount,\n'
              b'                     (void*)cmd, (unsigned)cmd->words.w0, (unsigned long long)cmd->words.w1);\n'
              b'            oot_ps3_early_trace(ps3BadCommand);\n'
              b'            oot_ps3_trace_resource_address(cmd);\n'
              b'            if (!g_exec_stack.gfx_path.empty()) {\n'
              b'                oot_ps3_early_trace("fast3d-resource: caller follows");\n'
              b'                oot_ps3_trace_resource_address(g_exec_stack.gfx_path.back());\n'
              b'            }\n'
              b'            oot_ps3_early_trace("fast3d-run: unsupported opcode; presenting partial frame");\n'
              b'            g_exec_stack.stop();\n'
              b'            break;\n'
              b'        }\n'
              b'        if (ps3StepCount <= 16 || (ps3StepCount % 1024) == 0) {\n'
              b'            char ps3CommandTrace[128];\n'
              b'            snprintf(ps3CommandTrace, sizeof(ps3CommandTrace),\n'
              b'                     "fast3d-run: step=%u cmd=%p w0=%08x w1=%016llx",\n'
              b'                     ps3StepCount, (void*)cmd,\n'
              b'                     cmd ? (unsigned)cmd->words.w0 : 0u,\n'
              b'                     cmd ? (unsigned long long)cmd->words.w1 : 0ull);\n'
              b'            oot_ps3_early_trace(ps3CommandTrace);\n'
              b'        }')
if run_source.count(run_loop) != 1:
    raise SystemExit('STOP: upstream Fast3D Run command loop changed')
run_source = run_source.replace(run_loop, loop_trace, 1)
flush_entry = b'\n    Flush();\n    mGfxFrameBuffer = 0;'
if run_source.count(flush_entry) != 1:
    raise SystemExit('STOP: upstream Fast3D Run flush changed')
run_source = run_source.replace(flush_entry,
    b'\n    oot_ps3_early_trace("fast3d-run: command loop returned; before Flush");'
    b'\n    Flush();'
    b'\n    oot_ps3_early_trace("fast3d-run: after Flush");'
    b'\n    mGfxFrameBuffer = 0;', 1)

# --- ps3 timing bisection: setup / command loop / Flush ---
setup_start_anchor = b'\n    oot_ps3_early_trace("fast3d-run: entered interpreter Run");'
if run_source.count(setup_start_anchor) != 1:
    raise SystemExit('STOP: ps3 timing setup-start anchor missing/duplicated')
run_source = run_source.replace(setup_start_anchor,
    setup_start_anchor +
    b'\n    unsigned long long ps3SetupStartUs = oot_ps3_microseconds_now();'
    b'\n    static unsigned long long ps3OpcodeTimeUs[256];'
    b'\n    static unsigned long ps3OpcodeCount[256];'
    b'\n    static int ps3PrevOpcodeValid = 0;'
    b'\n    static uint8_t ps3PrevOpcode = 0;'
    b'\n    unsigned long long ps3OpcodeTimerUs = 0;'
    b'\n    static unsigned int ps3TimeFrames = 0;'
    b'\n    const bool ps3TimeOps = ((++ps3TimeFrames & 15U) == 0U); /* sample 1 frame in 16 */', 1)
run_source = run_source.replace(run_signature,
    b'extern "C" unsigned long long oot_ps3_microseconds_now(void);\n' + run_signature, 1)

setup_end_anchor = b'    oot_ps3_early_trace("fast3d-run: after backend ClearFramebuffer");'
if run_source.count(setup_end_anchor) != 1:
    raise SystemExit('STOP: ps3 timing setup-end anchor missing/duplicated')
run_source = run_source.replace(setup_end_anchor,
    setup_end_anchor +
    b'\n    {'
    b'\n        char ps3SetupTiming[96];'
    b'\n        snprintf(ps3SetupTiming, sizeof(ps3SetupTiming), "graph: fast3d setup took %llu us", oot_ps3_microseconds_now() - ps3SetupStartUs);'
    b'\n        oot_ps3_early_trace(ps3SetupTiming);'
    b'\n    }', 1)

loop_start_anchor = b'    oot_ps3_early_trace("fast3d-run: display-list loop entered");\n'
if run_source.count(loop_start_anchor) != 1:
    raise SystemExit('STOP: ps3 timing loop-start anchor missing/duplicated')
run_source = run_source.replace(loop_start_anchor,
    loop_start_anchor +
    b'    unsigned long long ps3LoopStartUs = oot_ps3_microseconds_now();\n', 1)

flush_start_anchor = b'\n    oot_ps3_early_trace("fast3d-run: command loop returned; before Flush");'
if run_source.count(flush_start_anchor) != 1:
    raise SystemExit('STOP: ps3 timing flush-start anchor missing/duplicated')
run_source = run_source.replace(flush_start_anchor,
    b'\n    {'
    b'\n        char ps3LoopTiming[128];'
    b'\n        snprintf(ps3LoopTiming, sizeof(ps3LoopTiming), "graph: fast3d command loop took %llu us (%u steps)", oot_ps3_microseconds_now() - ps3LoopStartUs, ps3StepCount);'
    b'\n        oot_ps3_early_trace(ps3LoopTiming);'
    b'\n    }'
    b'\n    if (ps3PrevOpcodeValid) {'
    b'\n        unsigned long long ps3FinalOpUs = oot_ps3_microseconds_now();'
    b'\n        ps3OpcodeTimeUs[ps3PrevOpcode] += ps3FinalOpUs - ps3OpcodeTimerUs;'
    b'\n        ps3OpcodeCount[ps3PrevOpcode] += 1;'
    b'\n        ps3PrevOpcodeValid = 0;'
    b'\n    }'
    b'\n    static unsigned int ps3HistFrames = 0;'
    b'\n    const int ps3HistPrint = ((++ps3HistFrames % 1024U) == 0U);'
    b'\n    for (int ps3OpIdx = 0; ps3HistPrint && ps3OpIdx < 256; ++ps3OpIdx) {'
    b'\n        if (ps3OpcodeCount[ps3OpIdx] > 0 && ps3OpcodeTimeUs[ps3OpIdx] > 20000ULL) {'
    b'\n            char ps3OpLine[128];'
    b'\n            snprintf(ps3OpLine, sizeof(ps3OpLine), "graph: Play fast3d opcode=%02x total1024=%llu us count=%lu avg=%llu us",'
    b'\n                     ps3OpIdx, ps3OpcodeTimeUs[ps3OpIdx] * 16ULL, ps3OpcodeCount[ps3OpIdx] * 16UL,'
    b'\n                     ps3OpcodeTimeUs[ps3OpIdx] / ps3OpcodeCount[ps3OpIdx]);'
    b'\n            oot_ps3_early_trace(ps3OpLine);'
    b'\n        }'
    b'\n    }'
    b'\n    if (ps3HistPrint) {'
    b'\n        for (int ps3Z = 0; ps3Z < 256; ++ps3Z) { ps3OpcodeTimeUs[ps3Z] = 0; ps3OpcodeCount[ps3Z] = 0; }'
    b'\n    }' +
    flush_start_anchor +
    b'\n    unsigned long long ps3FlushStartUs = oot_ps3_microseconds_now();', 1)

flush_end_anchor = b'\n    oot_ps3_early_trace("fast3d-run: after Flush");'
if run_source.count(flush_end_anchor) != 1:
    raise SystemExit('STOP: ps3 timing flush-end anchor missing/duplicated')
run_source = run_source.replace(flush_end_anchor,
    b'\n    {'
    b'\n        char ps3FlushTiming[96];'
    b'\n        snprintf(ps3FlushTiming, sizeof(ps3FlushTiming), "graph: fast3d Flush took %llu us", oot_ps3_microseconds_now() - ps3FlushStartUs);'
    b'\n        oot_ps3_early_trace(ps3FlushTiming);'
    b'\n    }' +
    flush_end_anchor, 1)
# --- end ps3 timing bisection ---

source = source[:run_start] + run_source
pathlib.Path(sys.argv[2]).write_bytes(source)
PY

# ResourceManager.h declares shared_future but does not directly include its
# standard header. Force that header into this translation unit for diagnosis.
# Object mode allows a real linker-symbol and constructor review without
# changing the working ELF or PKG.
compiler_mode=(-fsyntax-only)
if [ "${OOT_PS3_FAST3D_OBJECT_MODE:-0}" = 1 ]; then
    command -v ppu-nm >/dev/null 2>&1 || { echo 'STOP: missing ppu-nm'; exit 1; }
    command -v ppu-readelf >/dev/null 2>&1 || { echo 'STOP: missing ppu-readelf'; exit 1; }
    mkdir -p "$(dirname "$OBJECT")"
    rm -f "$OBJECT"
    # Division by constants -> multiply, sqrt without errno: the Cell PPU divides slowly.
    compiler_mode=(-O2 -freciprocal-math -fno-math-errno -ffunction-sections -fdata-sections -c -o "$OBJECT")
fi
if "$CXX_BIN" -mminimal-toc -std=gnu++17 "${compiler_mode[@]}" -mcpu=cell -D__PS3__ \
    -include future -include algorithm \
    -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 -DIS_BIGENDIAN \
    -DCONTROLLERBUTTONS_T=uint32_t -DLOG_LEVEL_GAME_PRINTS=6 \
    -I"$ROOT/ps3_platform/include" -I"$ROOT/upstream_compat/include" \
    -I"$UPSTREAM/soh" -I"$UPSTREAM/soh/include" -I"$UPSTREAM/soh/src" \
    -I"$UPSTREAM/soh/soh" -I"$UPSTREAM/soh/assets" \
    -I"$UPSTREAM/libultraship/include" -I"$UPSTREAM/libultraship/src" \
    -I"$PS3_SDK_ROOT/ppu/include" "$PROBE_SOURCE" >"$OUT" 2>&1; then
    if [ "${OOT_PS3_FAST3D_OBJECT_MODE:-0}" = 1 ]; then
        echo "FAST3D INTERPRETER: real PS3 object compiled ($(wc -c < "$OBJECT") bytes)"
        "$CXX_BIN" -mminimal-toc -std=gnu++17 -O2 -mcpu=cell -D__PS3__ -include future \
            -ffunction-sections -fdata-sections \
            -I"$ROOT/ps3_platform/include" -I"$ROOT/upstream_compat/include" \
            -I"$UPSTREAM/libultraship/include" -I"$UPSTREAM/libultraship/src" \
            -I"$PS3_SDK_ROOT/ppu/include" \
            -c "$ROOT/ps3_platform/source/fast3d_resource_ps3.cpp" \
            -o "$RESOURCE_OBJECT" >"$ROOT/native-link-check/logs/fast3d-resource-compile.log" 2>&1 || {
            echo 'FAST3D PS3 RESOURCE BRIDGE: first compiler blockers'
            grep -m 5 -E 'fatal error:|error:' "$ROOT/native-link-check/logs/fast3d-resource-compile.log" || true
            exit 2
        }
        "$CXX_BIN" -mminimal-toc -std=gnu++17 -O2 -mcpu=cell -D__PS3__ \
            -ffunction-sections -fdata-sections \
            -I"$UPSTREAM/libultraship/include" \
            -c "$UPSTREAM/libultraship/src/fast/debug/GfxDebugger.cpp" \
            -o "$DEBUGGER_OBJECT" >"$ROOT/native-link-check/logs/fast3d-debugger-compile.log" 2>&1 || {
            echo 'FAST3D DEBUGGER: first compiler blockers'
            grep -m 5 -E 'fatal error:|error:' "$ROOT/native-link-check/logs/fast3d-debugger-compile.log" || true
            exit 2
        }
        "$CXX_BIN" -mminimal-toc -std=gnu++17 -O2 -mcpu=cell -D__PS3__ -include future \
            -DF3DEX_GBI_2 -DGBI_UCODE=F3DEX_GBI_2 -DIS_BIGENDIAN \
            -ffunction-sections -fdata-sections \
            -I"$ROOT/ps3_platform/include" -I"$ROOT/upstream_compat/include" \
            -I"$UPSTREAM/soh" -I"$UPSTREAM/soh/include" -I"$UPSTREAM/soh/src" \
            -I"$UPSTREAM/libultraship/include" -I"$UPSTREAM/libultraship/src" \
            -I"$PS3_SDK_ROOT/ppu/include" \
            -c "$ROOT/ps3_platform/source/fast3d_video_ps3.cpp" \
            -o "$VIDEO_OBJECT" >"$ROOT/native-link-check/logs/fast3d-video-compile.log" 2>&1 || {
            echo 'FAST3D VIDEO BACKEND: first compiler blockers'
            grep -m 5 -E 'fatal error:|error:' "$ROOT/native-link-check/logs/fast3d-video-compile.log" || true
            exit 2
        }
        echo 'Fast3D PS3 archive bridge, upstream debugger, and native video backend compiled'
        ppu-nm -u "$OBJECT" "$RESOURCE_OBJECT" "$DEBUGGER_OBJECT" "$VIDEO_OBJECT" |
            awk 'NF > 1 {print $NF}' | sort -u \
            >"$ROOT/native-link-check/fast3d-undefined.txt"
        echo "Fast3D and PS3 adapter external symbols: $(wc -l < "$ROOT/native-link-check/fast3d-undefined.txt")"
        if [ -f "$ROOT/native-link-check/oot-native.elf" ]; then
            ppu-nm -g --defined-only "$ROOT/native-link-check/oot-native.elf" \
                "$RESOURCE_OBJECT" "$DEBUGGER_OBJECT" "$VIDEO_OBJECT" \
                "$ROOT/ps3-resource-check/objects/ps3_platform_source_shipwright_resource_ps3.o" |
                awk 'NF {print $NF}' | sort -u >"$ROOT/native-link-check/fast3d-existing-symbols.txt"
            comm -23 "$ROOT/native-link-check/fast3d-undefined.txt" \
                "$ROOT/native-link-check/fast3d-existing-symbols.txt" \
                >"$ROOT/native-link-check/fast3d-not-in-current-elf.txt"
            echo "Absent after PS3 resource/debugger bridge (includes C++/SDK libraries): $(wc -l < "$ROOT/native-link-check/fast3d-not-in-current-elf.txt")"
            echo 'First missing Shipwright interfaces:'
            if command -v ppu-c++filt >/dev/null 2>&1; then
                ppu-c++filt <"$ROOT/native-link-check/fast3d-not-in-current-elf.txt" |
                    grep -E '^(Fast::|Ship::)' | head -n 20 || true
            else
                grep -E '^_ZN(4Fast|4Ship)' \
                    "$ROOT/native-link-check/fast3d-not-in-current-elf.txt" | head -n 20 || true
            fi
        fi
        if ppu-readelf -SW "$OBJECT" "$RESOURCE_OBJECT" "$DEBUGGER_OBJECT" "$VIDEO_OBJECT" |
            grep -E '\.(init_array|ctors)' >/dev/null; then
            echo 'Fast3D objects include pre-main sections: review before runtime integration'
        else
            echo 'Fast3D object has no pre-main constructor sections'
        fi
    else
        echo 'FAST3D INTERPRETER: syntax compile OK for PS3'
    fi
else
    echo 'FAST3D INTERPRETER (PS3 GUI/address/constexpr/log/settings guards): first compiler blockers'
    grep -m 5 -E 'fatal error:|error:' "$OUT" || sed -n '1,12p' "$OUT"
    exit 2
fi
