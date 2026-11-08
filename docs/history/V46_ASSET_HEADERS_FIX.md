# V46 Logo Header Packaging Fix

Restore these two source headers, unchanged, from the local Shipwright checkout
at revision `04ccd608b3884713bf6bf6284f5d272c256fc507`:

- `soh/assets/textures/nintendo_rogo_static/nintendo_rogo_static.h`
- `soh/assets/objects/gameplay_keep/gameplay_keep.h`

These are guards, an alignment-header include, resource path macros, and aligned
string declarations. They do not contain texture pixels, model geometry, or an
extracted ROM archive. Actual resources are still required at runtime.

The earlier blanket exclusion of `soh/assets` removed headers needed by the
logo state's source. Future full-project packages must retain these headers.

With the previous enum fix and these headers, all four boot-check sources
compiled to objects using the local desktop GCC, with the check script's flags
except `-mcpu=cell`. This checks source dependencies only, not PS3 code generation,
linking, archive loading, rendering, or execution. Re-run the original PPU check
in WSL with `bash scripts/check-real-boot.sh`.

This patch also carries the previously tested enum fix, so both V46 fixes remain
together. No runtime demo changes are included.
