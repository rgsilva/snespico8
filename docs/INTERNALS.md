# How snespico8 works

The one-sentence version: a real Lua interpreter and a full PICO-8 API, in
portable C, compiled for the 65816 by Calypsi, with the PICO-8 machine RAM
living 1:1 in WRAM bank $7F, the screen going out through Mode 7, the music
playing on the SPC700 in real time, and every byte of behavior pinned to the
z8lua oracle by a bit-exactness test net that has caught more compiler bugs
than core bugs.

There is no CPU emulation anywhere. PICO-8 carts are Lua source; the SNES
runs them the same way the desktop console does — by interpreting them.

## One core, two machines

`core-c/` is the whole console: z8lua (Lua 5.2 with the PICO-8 dialect),
fix32 numbers (`p8num.c`), the drawing API (`p8.c`, `p8gfx.c`) and the audio
model (`p8audio.c`). The same sources compile two ways:

- **Host** (`core-c/build/runp8`, clang/gcc `-O2 -fwrapv`): the correctness
  oracle and development loop. Runs carts headless, renders PPM frames,
  hashes RAM per flip (`-H`), measures heap use (`-M`).
- **SNES** (`snes/`, Calypsi cc65816): the same objects linked with the
  SNES-side glue — `p8main.c` (boot, game loop, menu, pause), Mode 7 video,
  `p8heap.c` (the SRAM-window allocator), `p8spc.*` (the APU driver),
  `hwmath.c` (CPU math units).

Everything interesting about correctness follows from this split: any
SNES-vs-host divergence is by construction a compiler or hardware-glue bug,
never ambiguity about what PICO-8 "should" do — the host side is separately
pinned to z8lua (149 printh vectors) and to real PICO-8 (`realvec.sh`).

## Numbers: fix32

PICO-8 numbers are 16.16 fixed point in a 32-bit word, wrapping. `luaconf.h`
sets `LUA_NUMBER` to a 32-bit integer and routes the primitive operations:

- add/sub/unm and the pure bit ops are **direct macros** in exact-32-bit
  types (`unsigned LUA_INT32` — on a 64-bit host a plain `unsigned long`
  would not wrap at 32 bits and would silently break parity; the fuzzer
  caught that in the macros' first draft).
- mul/div/mod/pow/shifts are far calls into `p8num.c`. `p8fix_toint` is the
  floor everyone pays (`(int16_t)(u >> 16)` — the high word read as signed
  16-bit *is* floor for all 2^32 inputs, proven exhaustively by fixfuzz).
- On the SNES (`-DP8_HWMATH`), the four 16x16 partials of `p8fix_mul` and
  small-integer division run on the CPU hardware multiplier/divider — see
  "The CPU math units" below.

Shift counts are the floored int of the fix32 operand, negated **as an
int** (z8lua semantics). The pre-M9 code negated the fix32 instead, which
trunc-rounded fractional counts (`16 << -1.5` was 8, oracle wants 4) and
never terminated on `-32768` (fix32 self-negation) — both fixed and pinned
by numvec vectors.

## The machine: bank $7F and the mailbox

PICO-8's 64 KB machine RAM (`p8_ram`) is WRAM bank $7F, byte-for-byte at the
console's own addresses: gfx at 0, map at $2000, draw state at $5F00, screen
at $6000. `peek`/`poke` are direct loads/stores.

The top 4 KB of bank $7E is the **test mailbox** (`snes/calypsi/mailbox.h`):
a stdout/stdin ring pair the Mesen harnesses poll, plus telemetry words at
**fixed addresses** — magic/status, `MB_FLIPS`, `MB_VMOPS` ($7E:F00C — the
Lua VM's instruction counter counts *straight into this slot* via
`P8_VMOPS_ADDR`, so benches sample it live), and the p8heap counters
($7E:F010-F018, published per flip). Fixed addresses are a hard rule: three
separate harness breakages (M8 test-input, the M9 bench, celeste.lua) came
from scripts reading `.bss` offsets copied out of a stale `p8.map`.

## The Lua heap: cartridge SRAM as RAM

`lua_newstate` allocates from `p8heap.c`, a first-fit free-list allocator
over a set of discontiguous regions: a 12 KB WRAM slice ($7E:C000) plus
every 8 KB HiROM SRAM window that answers a write-readback probe at boot
(`probe_sram()` — 256 KB on the flashcart profile we target). Celeste peaks
around ~100 KB of live heap; `runp8 -M` predicted the window budget on host
and the SNES agreed.

## Rendering: Mode 7 as a chunky framebuffer

Mode 7 is the only SNES mode with a byte-per-pixel tile format, which makes
it the honest way to show a 128x128 chunky screen. `video_init` uploads an
identity 16x16 tilemap; `snes_flip` (`p8main.c`):

1. **expand_screen**: the PICO-8 screen (2 px/byte, low nibble = left) is
   expanded to tile-major 8bpp via a 256-entry const `uint16_t` LUT — one
   indexed 16-bit store per source byte, row addressing hoisted to
   `sr += 64`. This was 91.6% of the frame before the LUT (M9 T0 profile).
2. A 16 KB DMA to VRAM under force-blank, then screen back on.

Letterboxing is window 1 clipping the right border and an HDMA channel
blanking BG1 below line 176. A game frame is ~11 SNES frames end to end
(~5.5 fps ceiling); the remaining weight is the per-frame Lua/C ceremony,
not the pixels.

Draw calls run through a per-shape **draw-context snapshot** (`P8Ctx`,
`p8int.h`): camera, clip, fillp, palette and the palette-resolved pen
nibbles are loaded once per API call and every pixel is camera-subtract +
clip-compare + fillp-bit on direct-page struct fields plus one screen-byte
RMW. The snapshot is semantically identical to the old per-pixel re-reads
because plotting can only write screen bytes, never draw state. `rectfill`
takes a byte-wise span path (memset middle, RMW nibble edges) when no fill
pattern is active.

## Audio: the SPC700 is the sound chip, really

The signature effect of the whole project: the game loop crawls, but the
music is in real time, because the SPC700 runs its own sequencer. At cart
load, `p8spc_load_audio` streams the cart's SFX/music data into APU RAM; the
core's `sfx()`/`music()` become two-byte "play" commands. The driver
(`p8spc.asm`, WLA-SPC700, timer-ticked ~121 Hz) sequences rows, chains
patterns, and applies all eight PICO-8 note effects SPC-side — fades via the
DSP envelope, slides/vibrato/drop/arp by per-tick pitch reprogramming. The
eight PICO-8 waveforms are looped BRR samples generated at build time
(`mkp8snd.py`).

## The CPU math units (`snes/hwmath.c`)

The 5A22 has an 8x8 multiplier ($4202/$4203 -> $4216) and a 16/8 divider
($4204-$4206 -> $4214) — separate silicon from the Mode 7 matrix registers
that `--no-ppu-mul` protects. The ROM runs NMI/IRQ-off, so nothing can
clobber a computation in flight. `p8hw_mul16` does 16x16->32 as four
latched partials; `p8hw_udiv32_8` divides by a byte in four chained steps
(remainder < k keeps every intermediate dividend in 16 bits). Wait margins
are interleaved volatile $4212 reads at ~3x the documented latency.

None of that is trusted on faith: `make test-abmath` runs the hardware
paths against the pure-C reference algorithms **on target** (edge cross,
random pairs, every divisor 1..255 both signs, all pow2), and snesparity
compares SNES-hardware-math against host-pure-C end to end. That corpus
caught two fresh compiler bugs before they shipped (see the minefield).

## The Calypsi minefield, and the fences around it

Six documented miscompiles so far (`bugs/`, `docs/M9-perf-plan.md`):

| # | What | Where it bit | Standing workaround |
|---|------|--------------|---------------------|
| 1 | -O2 comma-sequence misorder | `llex.c` | lexer/parser pinned -O0 |
| 2 | -O2 post-increment index | `llex.c` | same |
| 3 | const-ptr + offset sign-extends into bank $7E | `p8_reset` | chunked running pointer; `lstcheck` fence |
| 4 | -O1/-O2 allocation-size garbage in `lua_ltable` | boot panic | ltable pinned -O0 (`P8HOT` excludes it) |
| 5 | -O2 deletes volatile accesses through cast-constant pointers | `hwmath.c` | all hardware-register TUs -O0 |
| 6 | -O0 drops the truncation in `(uint8_t)(u32 >> K)` inside 16-bit expressions | `hwmath.c` divide chain | byte operands pre-masked into 16-bit locals |

The defense is layered, because one green run proves nothing against a
compiler whose failures are link-layout roulette:

1. **Host suites** (`make -C core-c check`): numvec/dialect/libvec diffed
   against a live z8lua, gfx and audio goldens byte-compared.
2. **fixfuzz** (`make -C core-c fuzz`): differential fuzzer for every p8num
   rewrite — exhaustive 2^32 toint, pow2-divisor sweeps, macro replicas.
   Self-limiting (in-binary wall deadline + `alarm()`), because a fuzzer
   that can hang is worse than no fuzzer — the shl(-32768) bug ran forever
   precisely where a deadline checkpoint couldn't fire.
3. **snesparity** (`core-c/tests/snesparity.sh`): every vector suite and
   seven hash carts, SNES vs host, printh lines and per-flip FB+RAM hashes
   bit-identical.
4. **The roulette battery** (`core-c/tests/roulette.sh`): fast parity
   across five perturbed link layouts (`PERTURB=n` pads shift every
   downstream symbol; verified by map diff). Required gate for
   codegen-sensitive changes (opt-level changes, asm-tier work).
5. **lstcheck fences** (`snes/tools/lstcheck.py`, runs before every link):
   scans every `.lst` for the negative-Y far-index signature and for
   bank-$7E high-half constants in core-c objects (bug #3's fingerprint).
6. **On-target A/B** (`make test-abmath`): hardware-unit math vs pure-C
   references, on the SNES, with the reference implementations kept
   permanently in `p8num.c` (linker-stripped from normal ROMs).

## Where to look

| Question | File |
|----------|------|
| Boot, game loop, flip, menu, pause | `snes/p8main.c` |
| Fixed telemetry / mailbox ABI | `snes/calypsi/mailbox.h` |
| fix32 semantics | `core-c/src/p8num.c`, `core-c/src/lua/luaconf.h` |
| Draw-context and per-pixel plotting | `core-c/src/p8.c`, `p8int.h` |
| Shape/sprite/map walkers | `core-c/src/p8gfx.c` |
| SRAM heap | `snes/p8heap.c` |
| Hardware math units | `snes/hwmath.c` |
| SPC driver + tooling | `snes/p8spc.asm`, `snes/tools/mkp8snd.py` |
| Cart embedding (.p8 and .p8.png) | `snes/tools/cart2c.py`, `p8png.py` |
| Perf history and plan | `docs/M9-perf-plan.md`, `docs/PERF.md` |
