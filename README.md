# Torneko: The Last Hope - native recompilation

A static recompilation of **World of Dragon Warrior: Torneko - The Last Hope** (PlayStation,
NTSC-U, `SLUS-01181`, Chunsoft / Enix, 2000) to native code, using the recompiler and runtime of
the Muppet Monster Adventure, Einhander and T.R.A.G. ports. The MIPS executable `SLUS_011.81` and
its code overlays are translated to C++ and run on Windows and Linux / SteamOS with a runtime
that replaces the PlayStation hardware and BIOS. Graphics are re-rendered with OpenGL at higher
resolution.

**No game data is included.** You need your own copy of the game as a disc image (`.chd`, or
`.cue` + both `.bin` tracks).

## Status

- All code is recompiled: the executable and the six code overlays in `DISK_MM.CD` (movie
  player, town, title/load, menus, ending and more). In the tested play (logos, intro movie,
  title, village name entry, the opening story, the first dungeon floors with combat and
  level-ups) no code runs in the interpreter, and frames match a reference emulator.
- Windows and Linux / SteamOS builds.

## Running

Windows: `Torneko-<version>-win64.zip` from the Releases page (or the `torneko-win64` artifact of
the latest **Build Windows and Linux** run).
Linux / Steam Deck: `torneko-<version>-linux-x86_64.tar.gz` (see its README.txt for adding it to
Steam as a non-Steam game).

1. Unpack it.
2. First run only: put your disc image next to `torneko.exe` (`torneko` on Linux) or in a `game`
   folder beside it, then run it - or drag the image onto it.
3. The disc is imported once into **`assets.dat`** (about 400 MB) and checked sector by sector
   to be an exact copy. From then on the game runs from `assets.dat` and the disc image can be
   deleted.

### In-game settings (F1 or Esc, Start+Select on a controller)

| Setting | Options |
|---|---|
| Resolution | 1x (original) to 6x internal resolution (restart); the window follows it (240 lines x the setting, shrunk to fit the desktop) |
| VSync, Fullscreen (F11 / Alt+Enter) | |
| Scaling | sharp or smooth |
| Texture filtering | off (original), smooth 3D (2D art and text stay sharp), smooth everything - bilinear on the palette-decoded texels, so transparent edges stay clean |
| Background upscaling | off (original pixels), smooth edges - the xBR pixel-art upscaler on screen-aligned 2D art drawn as polygons (on by default) |
| Colour | true colour, or the original 15-bit colour with the PlayStation's dither pattern |
| Precise geometry | sub-pixel polygon positions and perspective-correct textures (no wobble or texture warping), PGXP-style |
| Screen filter | off, scanlines, CRT (scanlines + aperture grille + glow; needs a window 600+ pixels tall) |
| Audio resampling | standard, or high quality (windowed-sinc) for the streamed XA music |
| Swap A/B and X/Y | face buttons by position |
| Fast CD loading | dungeon floors and towns load faster; music and movies stay real-time |
| Game speed | 1x-8x fast-forward (hold Tab for a burst) |
| Frame rate display | game frames per second and emulation speed (100% = a real console) |
| Keyboard keys | rebind every PlayStation button (or reset to defaults) |
| Controller buttons | rebind every PlayStation button to controller buttons, triggers or stick directions |

### Controls

Any XInput/SDL gamepad works; the left stick also drives the d-pad. The game's own DualShock
vibration is passed through (Vibration setting).
`gamecontrollerdb.txt` next to the exe adds more mappings. Start+Select opens the settings menu.
Keyboard defaults: arrows = d-pad, X = Cross, Z = Circle, A = Square, S = Triangle, Q/W = L1/R1,
1/2 = L2/R2, Enter = Start, Backspace = Select, I/J/K/L = right stick, Tab = fast-forward (hold).
Change them in the F1 menu under **Keyboard keys** (saved in `torneko.ini`).

Saves go to `memcard1.mcd` next to the exe, a standard 128 KB PlayStation memory card image
that emulators can use too.

## Regenerating the code

```sh
python3 tools/prepare_disc.py Torneko.cue game        # executable, archives, overlays/
python3 tools/gen_images.py game > config/images.txt  # (already committed)
python3 src/recomp.py game config src/gen 300
```

Windows build: see `.github/workflows/build.yml` (MinGW cross-compile + SDL2).
Linux desktop build: `cmake -S src -B build-linux -G Ninja -DUSE_SDL=ON && ninja -C build-linux`.
Technical details are in [NOTES.md](NOTES.md).

## Disclaimer

This is an unofficial fan project. It is **not affiliated with, endorsed by, or sponsored by**
Square Enix, Chunsoft (Spike Chunsoft), Sony Interactive Entertainment or any of their
affiliates. *Dragon Warrior*, *Torneko* and all related names and marks belong to their
respective owners. This repository contains no game data; you need your own legally obtained
copy of the game.

## License

The project's own code is released under the [MIT License](LICENSE). Third-party components keep
their own licenses; see [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
