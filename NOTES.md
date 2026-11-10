# Technical notes

## Binary layout

- `SLUS_011.81` (text 0x80010000-0x80121800, entry 0x8001E404): engine and PsyQ 4.5 libraries
  (libetc, libcd, libgpu, libgte, libspu/libsnd, libpad, libmcrd, libpress). No LoadExec.
- Files are packed into four archives; the executable has a table per archive: count, 0,
  archive name, 0, then per file {sector, size, sectors, 0, name}.
  - `DISK_MM.CD` (table 0x800B327C): the code overlays `MV.PRG` (movie player), `MK.PRG`,
    `DM.PRG`, `ED.PRG`, `LD.PRG`, `BN.PRG`, plus `DG.PRG` (shop/dialogue text, no code).
    The loader 0x800251A0 (name, destination, table, ...) is called with a fixed destination per
    overlay: MK/DG at 0x80128000, the others at 0x8013C000. `tools/gen_images.py` reads the
    table and writes `config/images.txt`; each image covers only the code at the start of the
    file (to the last `jr ra` before 24 KB without one).
  - `DISK_DG.CD` (table 0x800B1F50, 161 files: fonts, sound, characters, dungeon maps
    `MPK*.PRG`/`MPKBG*.PRG`), `DISK_MK.CD` (0x800B2BF4: town maps, NPCs, messages),
    `DISK_ED.CD` (0x800B3064: ending pictures). Despite the `.PRG` names these hold no code.
- Streams: `BGM0-4.STR` (XA music), `TORUNEKO.STR` (intro movie); track 2 is CD audio.
- Jump tables whose base register is set up before a loop (4 of them) are listed in
  `config/functions.txt`; 0x8013C0F0 is a function in MV.PRG but data in LD.PRG and BN.PRG.

## Code images and dispatch

Several images share addresses (the two executables at 0x80010000, overlays in each region).
For every image the recompiler emits a signature: instruction words spread over its code plus
words that differ from every other image covering the same addresses (only from functions found
by references, not by the gap sweep, so no data words). `lookup_func` (indirect calls,
`jalr`, calls between overlays) checks the signatures of the images covering the target (with a
small most-recently-used cache) and calls the recompiled function; code in no known image runs
in the interpreter. Direct calls inside an image, and from an overlay into `SYS.EXE`, are plain
C calls. Data words of an overlay (or of the executable) that point into another overlay loaded
alongside it (the engine's tables of stage handlers) are entry points too. `OVL_CAPTURE=dir` logs every interpreted entry point with RAM dumps, which is how
unknown overlays were located (`LOG=cdmap` logs which disc sector went to which address).

## Recompiler (`src/recomp.py`)

- Function discovery: entry point, `jal` targets, address-taken constants (`lui`/`addiu`),
  data words pointing at plausible function starts, and a final sweep of unreferenced
  `... jr ra` runs between known code. Iterates to a fixed point (~7100 functions over 25 images).
- One C++ function per guest function; branches become `goto`, calls become direct C calls,
  `jr ra` returns. Branch delay slots are emitted after the condition is evaluated. Jumps into
  the middle of a delay slot (hand-written asm loops) get a separate copy of the slot.
- R3000 load delay: a load followed by an instruction reading the same register is emitted with
  the old value visible to that instruction (the game's GCC code never needs it, but it is handled).
- Jump tables (`lui/addiu/addu/lw/jr`) become `switch`; other indirect jumps and `jalr` go through
  `rt_call`, which dispatches to recompiled code, HLE BIOS functions, or the interpreter.
- Cycle counting per basic block; `CHECK_EVENTS` at function entries and loop back-edges runs the
  hardware scheduler and takes interrupts.
- `config/functions.txt` `interp` entries run in the interpreter (`src/runtime/cpu.cpp`), for code
  that uses `ra` as a continuation pointer (none needed so far in this game).

## Runtime

- **BIOS** (HLE, `bios.cpp`): A0/B0/C0 functions used by the game, events, root counter events,
  interrupt chains (`SysEnqIntRP`) and libetc's `SetCustomExitFromException` dispatcher
  (`ReturnFromException` unwinds back to the interrupt point with a C++ exception).
  Kernel tables live in guest RAM at the real locations (table of tables at 0x100), and the
  runtime follows those pointers because PsyQ's `SetConf` re-allocates the event/thread tables.
  `SetConf` also decodes a `lui/addiu` pair at `[0x474]` to find BIOS variables; a scratch stub
  is provided there. `LoadExec` (A0:51) loads the next executable from the disc and restarts
  through a C++ exception that unwinds to the top level. The BIOS pad driver (`InitPAD`,
  `StartPAD`, `PAD_init`) fills the game's buffers every vblank; libapi's BIOS patches
  (`_patch_pad`, `_send_pad`, `EnablePAD`/`DisablePAD`) are no-ops. The game accepts pad ids
  0x41/0x53/0x73 but reads only the button bytes, and its `SendPAD` actuator buffers are
  initialised once and never written, so the port reports a digital pad (`PAD_ID=73` overrides).
- **Threads**: each guest thread runs
  on its own host thread; only one runs at a time (baton handoff), and the GL context follows.
  The game switches threads from its VSync interrupt by writing the PCB pointer directly, so the
  exception return resumes whatever TCB the PCB points at.
- **CD-ROM** (`cdrom.cpp`): register-level controller (libcd talks to it directly), 1x/2x sector
  timing, 0x800/0x924 sector modes, data FIFO + DMA3, XA-ADPCM decode with file/channel filter,
  CD-DA playback of the audio track. The BIOS leaves CD interrupts enabled (IE = 0x1F) at boot; libcd
  relies on that.
- **MDEC**: decode per psx-spx (RLE/quant, scale-table IDCT, YUV->RGB at 4/8/15/24 bit). DMA1 waits
  for decoded data; a new decode command discards unread output.
- **GPU**: GP0/GP1 command processing in `gpu.cpp`; `gpu_gl.cpp` renders into a (1024*S)x(512*S)
  RGBA8 VRAM texture (true colour, mask bit in alpha). Texture pages and CLUTs are sampled in the
  shader from a copy of VRAM refreshed only where drawing touched it. Semi-transparency via
  dual-source blending (B-F mode in two passes). 24-bit display mode decoded in a shader for movies.
- **Enhancements** (all optional): texture filtering in the draw shader (four palette-decoded
  texels blended among the opaque ones, the shape taken from the nearest texel, clamped to the
  primitive's own texel bounds so atlas neighbours do not bleed); original 15-bit colour with the
  4x4 dither matrix per original pixel; precise geometry (`precise.cpp`): the GTE keeps an
  unrounded copy of each projected vertex (X = OFX + IR1 * H / SZ with the unshifted MAC values)
  next to the SXY FIFO, `swc2` of SXY stores it in a shadow of the RAM word (the recompiler emits
  `gte_store_sxy`), and the GPU uses it for polygon vertices whose DMA source word still holds
  that value (or, for copied words, matches a projection from this frame); with the depth it also
  gives perspective-correct texturing. About 87% of in-game polygon vertices come out precise.
  XA audio can be resampled with a 32-tap Kaiser-windowed sinc instead of linear; the screen
  filter is part of the final scaling shader.
- **GTE**: full command set per psx-spx (UNR divide, flags, IR3 quirk, MVMVA far-colour bug).
- **SPU**: 24 voices, Gaussian interpolation (hardware table), ADSR/sweep envelopes, pitch
  modulation, noise, reverb with the 39-tap resampling FIR, CD audio input and capture buffers.
- **Controllers**: SIO0 with a DualShock (digital/analog/config mode, rumble mapping). Memory cards
  are handled at BIOS level (`memcard.cpp`) on standard 128 KB card images; libcard's BIOS patches
  are no-ops (`config/hle.txt`).

## Packed disc file (`assets.dat`, `src/runtime/discpack.cpp`)

The whole disc in one file (Torneko: a data track of 248,059 sectors and a CD audio track). The import reads a .chd
(libchdr) or a .cue/.bin. Header
(64 bytes: magic `MMAPACK` (format shared with the Muppet Monster port), version, track layout, block count, index offset, index CRC), then
deflate blocks of 16 sectors, then an index (offset, compressed size, size, CRC-32 per block).
A block holds one kind byte per sector followed by the payloads:

| kind | stored | rebuilt on read |
|------|--------|-----------------|
| 0 Mode 2 Form 1 | subheader + 2048 data | sync, MSF header, subheader copy, EDC, P/Q ECC |
| 1 Mode 2 Form 2 | subheader + 2324 data + EDC | sync, MSF header, subheader copy |
| 2 raw | 2352 | - (any sector that does not rebuild exactly) |
| 3 audio | 2352 | - |
| 4 zero | nothing | all zeros |

The import rebuilds every sector from its compact form and from the decompressed block and
compares it with the original, so the pack is bit-exact (an independent check against the .bin
files and a 2400-frame run from each source give identical RAM and frames). The 653 MB .bin
(or 331 MB .chd) becomes 302 MB; a .chd and a .cue/.bin of the same disc give an identical file. The reader keeps the last decoded block; the CD controller reads through the
same `read_raw` interface either way.

## Notable fixes (runtime shared with the Muppet Monster Adventure port, found by comparing against a reference emulator)

- Jump-table case labels must never become function entries (the data-pointer scan used to pick
  them up from `.rodata`); otherwise the dialogue state machine broke and the Hub cutscene hung.
- Overlays such as HUB1 are entered only through run-time pointers: the gap sweep covers the whole
  overlay image.
- GTE 16-bit data registers (IR0-3, VZ, SZ, OTZ) keep only the low half of the written word. The
  GTE was checked against an independent implementation on millions of random register states.
- Byte writes to DICR (libcd updates the enable byte with `sb`) must not acknowledge pending DMA
  IRQ flags; otherwise MDEC slice interrupts were lost and movies dropped frames.
- Memory cards: libmcrd looks up the "bu" device in the BIOS device table and hooks its
  `firstfile`; async file I/O returns 0 at once and completes by event from a later interrupt.
- BIOS string arguments can live in the scratchpad (the game's stack is there).
- BIOS threads: a retired host thread is told apart from its replacement by a generation number.

- libetc `VSync()` waits in a polling loop that gives up after `count << 15` passes. Recompiled
  code charged one cycle per instruction, so a one-frame wait timed out before the vblank:
  `VSync()` returned early (the game loop ran about twice as fast: characters moved too fast, the
  title screen timed out early) and the timeout path called `ChangeClearPAD(0)`. `hle_vsync_wait`
  replaces the loop and measures the timeout at the console's ~30 cycles per pass.
- Backdrops are drawn partly as screen-aligned textured quads and partly as sprites; "smooth 3D"
  texture filtering now treats depth-less screen-aligned quads as 2D art, so backdrops stay uniform.

## Debugging aids

- `torneko.log` next to the exe lists code the game ran outside the recompiled images (it
  still works, in the interpreter); it only appears when that happens.
- `POKE=addr=value,...` (from frame `POKE_FROM`) writes RAM bytes every frame.

- `LOG=bios,cd,irq,thread,ovl,interp,dma,gpu,disp,mdec,...` category logging.
- `SHOT_DIR`, `SHOT_EVERY`, `SHOT_FROM`, `FRAMES`, `NO_THROTTLE`, `STOP_AT_FRAME` (SIGTRAP for gdb;
  generated code is built with line tables so host backtraces map to guest labels).
- `WATCH=800bb224,gp+178c` prints RAM words when they change; `RAM_DUMP_AT=frame:file` dumps RAM.
- Bisect build (`-DCMAKE_CXX_FLAGS=-DRECOMP_BISECT`): `BISECT=lo-hi` runs those functions in the
  interpreter (from `BISECT_FROM_SEC`), to find code generation bugs.
- `MDEC_DUMP=1` writes MDEC input per frame; `tools/bs_decode.py` decodes a frame of an STR file
  independently for comparison.
