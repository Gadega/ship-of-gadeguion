#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT="$ROOT/native-link-check/fast3d-rsx-shaders.h"
mkdir -p "$ROOT/native-link-check"
command -v cgcomp >/dev/null || { echo 'BLOCKED: cgcomp is required for the native RSX shaders'; exit 1; }
CGCOMP_BIN="$(command -v cgcomp)"
if [ -s "$OUTPUT" ] && [ -s "$ROOT/native-link-check/fast3d_ps3.vpo" ] &&
   [ -s "$ROOT/native-link-check/fast3d_ps3.fpo" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3.fcg" -nt "$OUTPUT" ] &&
   [ -s "$ROOT/native-link-check/fast3d_ps3_generic.fpo" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_generic.fcg" -nt "$OUTPUT" ] &&
   [ -s "$ROOT/native-link-check/fast3d_ps3_generic_clip.fpo" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_generic_clip.fcg" -nt "$OUTPUT" ] &&
   [ -s "$ROOT/native-link-check/fast3d_ps3_tnl.vpo" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl.vcg" -nt "$OUTPUT" ] &&
   [ -s "$ROOT/native-link-check/fast3d_ps3_tnl_probe.vpo" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_probe.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_attrprobe.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_attrprobe2.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest1.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest2.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest3.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest4.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest5.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest6.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest7.vcg" -nt "$OUTPUT" ] &&
   [ ! "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest8.vcg" -nt "$OUTPUT" ] &&
   [ ! "$CGCOMP_BIN" -nt "$OUTPUT" ]; then
    echo 'FAST3D RSX: unchanged shader binaries reused'
    exit 0
fi
cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3.vcg" "$ROOT/native-link-check/fast3d_ps3.vpo"
cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl.vcg" "$ROOT/native-link-check/fast3d_ps3_tnl.vpo"
cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_probe.vcg" "$ROOT/native-link-check/fast3d_ps3_tnl_probe.vpo"
cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_attrprobe.vcg" "$ROOT/native-link-check/fast3d_ps3_tnl_attrprobe.vpo"
cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_tnl_attrprobe2.vcg" "$ROOT/native-link-check/fast3d_ps3_tnl_attrprobe2.vpo"
for t in 1 2 3 4 5 6 7 8; do
    cgcomp -v -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_selftest$t.vcg" "$ROOT/native-link-check/fast3d_ps3_selftest$t.vpo"
done
cgcomp -f -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3.fcg" "$ROOT/native-link-check/fast3d_ps3.fpo"
cgcomp -f -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_generic.fcg" "$ROOT/native-link-check/fast3d_ps3_generic.fpo"
cgcomp -f -Wcg,-strict "$ROOT/ps3_platform/shaders/fast3d_ps3_generic_clip.fcg" "$ROOT/native-link-check/fast3d_ps3_generic_clip.fpo"
python3 - "$ROOT/native-link-check/fast3d_ps3.vpo" "$ROOT/native-link-check/fast3d_ps3.fpo" "$ROOT/native-link-check/fast3d_ps3_generic.fpo" "$ROOT/native-link-check/fast3d_ps3_generic_clip.fpo" "$ROOT/native-link-check/fast3d_ps3_tnl.vpo" "$ROOT/native-link-check/fast3d_ps3_tnl_probe.vpo" "$ROOT/native-link-check/fast3d_ps3_tnl_attrprobe.vpo" "$ROOT/native-link-check/fast3d_ps3_tnl_attrprobe2.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest1.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest2.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest3.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest4.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest5.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest6.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest7.vpo" "$ROOT/native-link-check/fast3d_ps3_selftest8.vpo" "$OUTPUT" <<'PY'
import pathlib, sys
vpo, fpo, fpog, fpoc, vpotnl, vpoprobe, vpoattr, vpoattr2, st1, st2, st3, st4, st5, st6, st7, st8, out = map(pathlib.Path, sys.argv[1:])
with out.open('w') as target:
    target.write('/* Generated locally from our own generic Cg shader sources. */\n')
    for path, name in ((vpo, 'oot_ps3_rsx_vpo'), (fpo, 'oot_ps3_rsx_fpo'), (fpog, 'oot_ps3_rsx_fpo_generic'), (fpoc, 'oot_ps3_rsx_fpo_generic_clip'), (vpotnl, 'oot_ps3_rsx_vpo_tnl'), (vpoprobe, 'oot_ps3_rsx_vpo_tnl_probe'), (vpoattr, 'oot_ps3_rsx_vpo_tnl_attrprobe'), (vpoattr2, 'oot_ps3_rsx_vpo_tnl_attrprobe2'), (st1, 'oot_ps3_rsx_vpo_selftest1'), (st2, 'oot_ps3_rsx_vpo_selftest2'), (st3, 'oot_ps3_rsx_vpo_selftest3'), (st4, 'oot_ps3_rsx_vpo_selftest4'), (st5, 'oot_ps3_rsx_vpo_selftest5'), (st6, 'oot_ps3_rsx_vpo_selftest6'), (st7, 'oot_ps3_rsx_vpo_selftest7'), (st8, 'oot_ps3_rsx_vpo_selftest8')):
        data = path.read_bytes()
        if not data or len(data) > 1024 * 1024:
            raise SystemExit('BLOCKED: RSX shader has an invalid compiled length')
        target.write('static const unsigned char %s[] __attribute__((aligned(64))) = {\n' % name)
        for start in range(0, len(data), 16):
            target.write('    ' + ','.join('0x%02x' % b for b in data[start:start+16]) + ',\n')
        target.write('};\n')
print('FAST3D RSX: generic vertex and fragment shaders compiled')
PY
