# V46 Real Boot Closure Check

`scripts/check-real-closure.sh` first compiles the fourteen-source boot baseline,
then adds 34 upstream provider units selected from its unresolved symbols. The
48 real Shipwright C files include the boot units, game and graphics loops,
scheduler, title/opening states, the desktop N64 compatibility layer, arenas,
matrix/view code, display-list setup, VI state, DMA, controller manager, and
supporting engine modules.

A relocatable link verifies that the selected objects can coexist and detects
duplicate definitions. It deliberately permits external references. The symbol
inventory is the work list for choosing additional upstream objects, PSL1GHT
bindings, and PS3 platform implementations. The report separately states how
many baseline symbols the provider wave resolved because adding real modules can
legitimately expose a larger next-level dependency frontier.

This is not an executable link and cannot be installed or run. It contains no
ROM, generated O2R archive, texture pixels, or model geometry.
