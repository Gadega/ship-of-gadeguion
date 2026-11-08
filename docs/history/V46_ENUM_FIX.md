# V46 Enum Compatibility Fix

Changes only the default expansion in upstream Shipwright's
`soh/soh/Enhancements/randomizer/randomizerEnums.h`.

Replace `__VA_OPT__` with a one-argument/two-argument macro selector. A one-argument
item retains implicit numbering; a two-argument item retains its explicit value.
All helper macros are removed alongside the original default macros. Custom
X-macro expansions are left unchanged.

Validation: all enum declarations preprocessed identically (ignoring whitespace)
before and after the change using local GCC in GNU C11 and GNU C++17 modes.
Both patched header syntax checks passed. The PS3 GCC 7 toolchain is not available
in the development container; rerun `bash scripts/check-real-boot.sh` in WSL.

This does not link or boot the game. Further compilation blockers may follow.
