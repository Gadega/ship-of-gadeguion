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

- The whole game: title screen and demo, file select, saving and loading, the full adventure
- Music and sound effects (mixed on an SPU), with day/night music, nature ambience and weather
- 60 fps with frame interpolation (vertex work on the RSX, rendering and game logic on separate threads)
- 4:3 and 16:9 TVs, including CRTs at 480i/480p (follows the XMB *TV Type* setting)
- Saves stored as regular PS3 save data (they show up in the XMB's Saved Data Utility)
- Save states (3 slots), rumble on the DualShock 3, volume controls, an in-game menu (SELECT)
- An optional debug menu (warps, cutscene player, items, age switch, performance info, screenshots)

## What you need

- A PS3 running **HEN** (or CFW) that can install PKG files
- Your own OoT ROM, **NTSC-U 1.0** (other versions are untested and may not work)
- **[Ship of Harkinian](https://github.com/HarbourMasters/Shipwright/releases) 9.2.x** on a PC,
  to turn the ROM into `oot.o2r` (only once)
- A USB stick formatted as **FAT32** (the PS3 can't read exFAT or NTFS)

## Installing

1. **Make `oot.o2r`.** Run Ship of Harkinian 9.2.x on your PC once and give it your ROM. It
   creates `oot.o2r` next to `soh.exe`. (Use `oot.o2r`, not `soh.o2r`.)
2. **Copy it to a USB stick.** Any file name ending in `.o2r` works, in the root of the stick or
   in a folder.
3. **Install the PKG.** Download `Ship of Gadeguion v<version>.pkg` from the [Releases](../../releases)
   page and install it with HEN's Package Manager (*Install Package Files*).
4. **Start Ship of Gadeguion** with the USB stick plugged in. The setup screen finds your file,
   checks it and copies it to the PS3 (press X to install). The XMB icon then turns into the
   game's title logo, drawn from your own game data.

**Updating:** install the new PKG over the old one. Your game file, settings and saves are kept,
so the setup screen doesn't come back. (Hold **Triangle** while the game starts to open it again,
for example to replace the game file.)

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

**SELECT** opens the Ship of Gadeguion menu: save states, volumes, rumble, the debug menu switch,
reset to the title screen and *Quit to XMB*. **R3** opens the debug menu once it's switched on.

## Known issues

- Only the NTSC-U 1.0 ROM has been tested.
- A subtle audio stutter can still be heard at times.
- Frame rate can dip in some heavy views at 1080p.
- Ship of Harkinian's enhancements (its PC menu options, randomizer, texture packs, custom
  models) are not available yet, and mod support is still to come.
- Save states are kept only until you quit the game.
- Much of the game hasn't been played through on this port yet. Bug reports are welcome.

## Reporting bugs

The game keeps a log by default. If it crashes or freezes, it saves a crash log, and if a USB
stick is plugged in it also writes it there:

- `ShipOfGadeguion-log.txt`: the current session
- `ShipOfGadeguion-crash-<date>.txt`: one file per crash (also created after a hard freeze, the
  next time you start the game)

Open an [issue](../../issues) with what happened and where, and attach the crash file (and a
screenshot if you can: the debug menu saves them to the USB stick too). On the PS3 the logs are in
`/dev_hdd0/game/OOTP30048/USRDIR/logs/`. Logs can be switched off in the debug menu.

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
EBOOT ends up in `build-real-native-pkg/pkg/USRDIR/EBOOT.BIN`, and the installable PKG (no game
data) is `oot_ps3_probe_v48.pkg`. Set `OOT_PS3_DEBUG_MENU=0` for release builds (the debug
menu is then off until the player switches it on in the SELECT menu).

## How it works (short version)

- `upstream/shipwright/`: Ship of Harkinian, libultraship and Torch, with PS3 changes marked
  `__PS3__`.
- `ps3_platform/`: the PS3 layer: RSX renderer backend for libultraship's Fast3D, resource
  loader for `.o2r` archives (big-endian conversion of every resource type), audio output and
  the SPU mixer, PS3 save data, input, threads and timers.
- `ps3_platform/source/ps3_setup_screen.c`: the first-boot setup that installs the player's
  `oot.o2r` from a USB stick and draws the XMB icon from it.
- `tools/packager/`: the old PC packager (up to v1.1.0; no longer needed).

## Credits

- [Ship of Harkinian](https://github.com/HarbourMasters/Shipwright) by HarbourMasters and contributors
- [libultraship](https://github.com/Kenix3/libultraship) and [Torch](https://github.com/HarbourMasters/Torch)
- The [OoT decompilation](https://github.com/zeldaret/oot) team
- [PSL1GHT](https://github.com/ps3dev/PSL1GHT) and the [ps3dev](https://github.com/ps3dev/ps3dev) toolchain
- [stb_image and stb_truetype](https://github.com/nothings/stb) by Sean Barrett
- [DejaVu fonts](https://dejavu-fonts.github.io/) (menu text)
- [font8x8](https://github.com/dhepper/font8x8) by Daniel Hepper (debug menu text)
- PS3 port by **Gadega**

## License

The PS3 port's own code is released under the GNU General Public License v3 (see `COPYING`).
Bundled third-party code keeps its own license (libultraship and Torch: MIT; stb: public domain/MIT;
DejaVu fonts: Bitstream Vera license, see `third_party/dejavu/LICENSE`;
Ship of Harkinian and the decompilation: see their projects). The original game is © Nintendo;
no game assets are included.
