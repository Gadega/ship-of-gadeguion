# Ship of Gadeguion

**Ship of Harkinian, running natively on the PlayStation 3.**

Ship of Gadeguion is a native PS3 port of [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright),
the PC port of OoT built on the [OoT decompilation](https://github.com/zeldaret/oot).
It is not an emulator: the game's C code is compiled for the PS3's Cell processor, graphics are
drawn on the RSX and the music and sound effects are mixed on an SPU.

> **No game data is included.** You need your own copy of the game, converted with Ship of
> Harkinian into `oot.o2r`. This project does not and will not distribute Nintendo's assets.

This is an unofficial fan project. It is not affiliated with or endorsed by Nintendo, Sony,
HarbourMasters (Ship of Harkinian) or the decompilation team.

## What works

- The whole game boot flow: title screen and demo, file select, saving and loading
- Playing the game: tested from the start of a new file through Kokiri Forest
- Music and sound effects (mixed on an SPU)
- 60 fps with frame interpolation in most scenes (heavy scenes drop lower)
- 4:3 and 16:9 TVs, including CRTs at 480i/480p (follows the XMB *TV Type* setting)
- Saves stored as regular PS3 save data (they show up in the XMB's Saved Data Utility)
- The pause menu, including the 3D Link on the equipment screen

## What you need

- A PS3 running **HEN** (or CFW) that can install PKG files
- Your own OoT ROM, **NTSC-U 1.0** (other versions are untested and may not work)
- **[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright/releases) 9.2.x** on a PC,
  to turn the ROM into `oot.o2r`
- A Windows PC to run the packager, or a Mac/Linux PC with Python 3

## Installing

1. **Make `oot.o2r`.** Run Ship of Harkinian 9.2.x on your PC once and give it your ROM. It
   creates `oot.o2r` next to `soh.exe`. (Use `oot.o2r`, not `soh.o2r`.)
2. **Build the PKG.** Download `ShipOfGadeguion-Packager-<version>.exe` from the
   [Releases](../../releases) page, open it, pick your `oot.o2r` and click *Build PKG*.
   It checks your file, then writes an installable PKG on your PC. The XMB icon and name are made
   from your own game data.
   **Mac or Linux:** download `ShipOfGadeguion-Packager-<version>-Python.zip` instead, unzip it and
   run `python3 oot_ps3_packager.py` (needs Python 3; instructions inside).
3. **Install it** on the PS3 with HEN's Package Manager (*Install Package Files*), like any other PKG.
4. Start **Ship of Gadeguion** from the Game column.

To update to a newer version, build a new PKG with the new packager and install it over the old
one. Your saves are kept.

## Controls

| N64 | DualShock 3 |
| --- | --- |
| Control stick | Left stick |
| A | Cross |
| B | Square |
| Start | Start |
| Z | L2 |
| R | R1 |
| L | L1 |
| C-Up | Triangle or right stick up |
| C-Right | Circle or right stick right |
| C-Down | R2 or right stick down |
| C-Left | Right stick left |
| D-pad | D-pad |

**SELECT quits the game** and returns to the XMB (so does *PS button → Quit Game*).

## Known issues

- Only the NTSC-U 1.0 ROM has been tested.
- Frame rate drops in busy areas; the renderer still does most of its work on the Cell's main core.
- Some environment effects are recreated by hand rather than by the original code, so weather and
  lighting can differ from Ship of Harkinian in places.
- Small audio crackles are possible.
- Ship of Harkinian's menus and enhancements (the PC "Ship" menu, randomizer, texture packs,
  custom models) are not available.
- Much of the game beyond the opening areas hasn't been played on this port yet. Bug reports are
  welcome.

## Reporting bugs

Please open an [issue](../../issues) with what happened, where in the game, and a photo or
screenshot if you can. A trace file helps a lot:

1. Using FTP (for example FileZilla), create an empty file named `trace_on` in
   `/dev_hdd0/game/OOTP30048/USRDIR/`.
2. Play until the problem happens. If the game freezes, wait about 20 seconds: it records where it
   stopped and returns to the XMB.
3. Attach `/dev_hdd0/game/OOTP30048/USRDIR/audio_probe_boot_trace.txt` to the issue.

Delete `trace_on` afterwards; writing the trace slows the game down a little.

## Building from source

You need Linux (or WSL on Windows) with:

- The [ps3dev](https://github.com/ps3dev/ps3dev) toolchain with
  [PSL1GHT](https://github.com/ps3dev/PSL1GHT) installed in `/usr/local/ps3dev`
  (`PS3DEV` and `PSL1GHT` set, `ppu-gcc`, `spu-gcc`, `cgcomp`, `make_self_npdrm`, `pkg` and `sfo` on the `PATH`)
- Python 3

Then, from the repository root:

```bash
bash scripts/check-real-core-c.sh          # game code (about 700 C files)
bash scripts/check-real-essential-cpp.sh   # C++ support code
bash scripts/check-real-ps3-services.sh    # PS3 platform services
bash scripts/check-real-native-link.sh     # renderer, audio, platform layer; final link
bash scripts/package-real-native.sh        # EBOOT.BIN + a test PKG (no game data)
```

Each script prints `OK` on success and writes logs under its own `*-check/` folder. The
EBOOT ends up in `build-real-native-pkg/pkg/USRDIR/EBOOT.BIN` and is also copied into
`tools/packager/payload/`, so the packager script uses it. To build the Windows packager
`.exe`, run `tools/packager/build_exe.bat` on Windows (it uses PyInstaller).

## How it works (short version)

- `upstream/shipwright/`: Ship of Harkinian, libultraship and Torch, with PS3 changes marked
  `__PS3__`.
- `ps3_platform/`: the PS3 layer: RSX renderer backend for libultraship's Fast3D, resource
  loader for `.o2r` archives (big-endian conversion of every resource type), audio output and
  the SPU mixer, PS3 save data, input, threads and timers.
- `tools/packager/`: the PKG builder that combines the port with the user's own `oot.o2r`.

## Credits

- [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters and contributors
- [libultraship](https://github.com/Kenix3/libultraship) and [Torch](https://github.com/HarbourMasters/Torch)
- The [OoT decompilation](https://github.com/zeldaret/oot) team
- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and the [ps3dev](https://github.com/ps3dev/ps3dev) toolchain
- [stb_image](https://github.com/nothings/stb) by Sean Barrett
- PS3 port by **Gadega**

## License

The PS3 port's own code is released under the GNU General Public License v3 (see `COPYING`).
Bundled third-party code keeps its own license (libultraship and Torch: MIT; stb: public domain/MIT;
Ship of Harkinian and the decompilation: see their projects). The original game is © Nintendo;
no game assets are included.
