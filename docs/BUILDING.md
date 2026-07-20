# Building & running snespico8

Two halves build independently:

- **`core-c/`** — the portable PC reference core (the same C the SNES build
  compiles). Needs only a C compiler. This is where the fast test suites run.
- **`snes/`** — the 65816 ROM: the core compiled with Calypsi plus the
  WLA-DX glue, verified in Mesen2.

## Prerequisites

| Tool | Used for | Where the build looks |
|------|----------|-----------------------|
| a C compiler (`cc`/gcc/clang) | `core-c` host build + suites | `$CC` (default `cc`) |
| **z8lua** | oracle for the language suites | `tools/z8lua/z8lua` |
| **Calypsi** 65816 C compiler | the SNES ROM (`cc65816`/`ln65816`) | `tools/calypsi-65816-5.17/` |
| **WLA-DX** (`wla-spc700`, `wlalink`) | the SPC700 driver + link | `../pvsneslib/devkitsnes/bin/` |
| **Mesen2** | running the SNES tests | `$(MESEN)` = `/Applications/Mesen.app/…` |
| **PICO-8** (optional) | exporting `.p8.png`, the real-PICO-8 oracle | `/Applications/PICO-8.app/…` |

Calypsi is licensed **personal, non-commercial (hobby)** use only and may
**not be redistributed** — hence it is gitignored, and CI cannot build the
ROM (see M14). z8lua is open source (`samhocevar/z8lua`, build with `make`);
build it into `tools/z8lua/` so the suites find `tools/z8lua/z8lua`.

## Host core (no Calypsi needed)

```sh
make -C core-c            # build runp8 (the headless host runner)
make -C core-c check      # full local suite (see below)
make -C core-c check-ci   # the host-portable subset that CI runs
make -C core-c test       # smoke: render one cart to a PPM
```

The suites:

| suite | checks | needs | in CI? |
|-------|--------|-------|:------:|
| `numvec`   | 48 fix32 expressions vs the z8lua oracle | z8lua | ✔ |
| `dialectvec` | 27 P8-dialect statements vs z8lua | z8lua | ✔ |
| `gfxvec`   | 6 gfx carts vs committed per-flip hashes | — | ✔ |
| `audiovec` | the audio command/stat trace golden | — | ✔ |
| `libvec`   | 74 library outputs vs z8lua | z8lua | local only |

`libvec` is **local/reference only**: it probes `sin`/`cos` at their extremes,
where z8lua overshoots to ±1.1055 (a fixed-point quirk our core reproduces on
clang and on the 65816 — bit-exact per M4). gcc rounds it to ±1, so the suite
is host-compiler sensitive and can't gate a Linux CI runner. The core also
builds with `-fwrapv` so its 2's-complement fixed-point math is deterministic
across host compilers.

Run `runp8` directly for a single cart:
`core-c/build/runp8 cart.p8 -n <frames> -p out.ppm [-i inputs.txt]`.

## SNES ROM tests (need Calypsi + Mesen)

Each builds a ROM and runs a Mesen Lua harness that asserts `RESULT PASS`.

```sh
make -C snes test-p8        # M3: boots a cart, framebuffer pixel-exact vs the oracle
make -C snes test-spc       # M6a/b: SPC upload + 8 waveforms in tune
make -C snes test-spcseq    # M6c: the autonomous SPC sequencer
make -C snes test-audio     # M6d: a cart's music() drives the SPC end to end
make -C snes test-fx        # M6-fx: per-tick pitch/envelope effects
make -C snes test-input     # M7: the real pad reaches btn() (setInput-driven)
make -C snes test-menu      # M8a: the multi-cart menu (launch + return)
make -C snes test-png       # M8b: a ROM built straight from a .p8.png renders
make -C snes test-demo      # M13: the curated demo ROM's menu + a cart
make -C snes test-celeste   # M5c: Celeste boots (fetch the cart first, see the target)
```

## Loadable ROMs (open in Mesen, or flash)

```sh
make -C snes demo                       # M6 audio demo (sound on)
make -C snes play [PLAYCART=cart.p8]    # M7 interactive: drive it with a pad
make -C snes menu [MENUCARTS="a.p8 b.p8"]# M8a picker over a cart set
make -C snes demo-rom                   # M13 curated demo (video/joypad/audio)
make -C snes release                    # M14 -> build/snespico8.sfc (the public ROM)
```

`CART=…` and `FRAMES=…` parameterise the generic `p8` target; a cart may be a
`.p8` or a `.p8.png` (decoded at build time — see `cart-formats.md`).

## Release (M14)

CI (`.github/workflows/build.yml`) does it end to end. On every push it runs
the host suites (`make -C core-c check-ci`) **and** builds the flashable ROM —
it installs Calypsi from the author's official release and builds WLA-DX from
source. On a `v*` tag it publishes a GitHub release with `snespico8.sfc`
attached, so cutting a release is just:

```sh
git tag v0.1 && git push --tags
```

The CI ROM is **byte-identical** to a local `make -C snes release` (the
cross-compile is deterministic), which you can still run to build/test the ROM
yourself. It ships only our own carts (no Lexaloffle/BBS assets), so it is
freely redistributable.
