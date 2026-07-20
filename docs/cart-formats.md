# Cart formats: `.p8` and `.p8.png`

The build tools (`snes/tools/cart2c.py`, `multicart2c.py`) accept a cart as
either a `.p8` text file or a `.p8.png` image, and turn it into the same C
arrays baked into the ROM. **All of this happens on the PC at build time —
the SNES never parses a cart, and never sees a PNG.** It receives the decoded
Lua source plus the raw asset sections.

## Cart memory image (0x0000–0x7FFF)

Both formats describe the same 32 KB cart image, which maps 1:1 into PICO-8
RAM (`$7F:` on the SNES):

| range | size | section |
|-------|------|---------|
| `0x0000`–`0x1FFF` | 0x2000 | sprite sheet (gfx) |
| `0x2000`–`0x2FFF` | 0x1000 | map |
| `0x3000`–`0x30FF` | 0x0100 | sprite flags (gff) |
| `0x3100`–`0x31FF` | 0x0100 | music |
| `0x3200`–`0x42FF` | 0x1100 | sfx |
| `0x4300`–`0x7FFF` | 0x3D00 | Lua code (raw or compressed) |

## `.p8` (text)

A UTF-8 file of tagged sections: `__lua__` (source), `__gfx__`, `__map__`,
`__gff__` (hex nibble rows), `__sfx__`, `__music__` (PICO-8's hex notation).
`cart2c.parse_cart` decodes each into the section bytes above. This is the
authoring format and the one our own carts ship as.

## `.p8.png` (image)

A 160×205 32-bit-RGBA PNG — the cart's little cover picture with the 32 KB
image **steganographically hidden in the low two bits of every channel**.
`snes/tools/p8png.py` decodes it (verified byte-exact against real PICO-8
exports):

1. **PNG decode** — parse IHDR/IDAT, zlib-inflate, undo the five scanline
   filters (None/Sub/Up/Average/Paeth). Done with `zlib` only, no Pillow.
2. **De-stego** — one cart byte per pixel, from the low 2 bits of each
   channel: `byte = (A&3)<<6 | (R&3)<<4 | (G&3)<<2 | (B&3)`. 160×205 = 32800
   pixels → the 32 KB image (the first 0x8000 bytes) plus a few spare.
3. **Decompress the code** at `0x4300` (below).

Unused sfx/music slots in a `.p8.png` may hold leftover PICO-8 editor RAM
rather than zeros — harmless, since a cart never reads slots it didn't define.

## Code compression

The bytes at `0x4300` are one of:

- **raw** — plain source, NUL-terminated (rare; tiny carts).
- **old `:c:`** — the pre-0.2 format. Not implemented; `p8png` errors with a
  clear message asking you to re-save in a recent PICO-8.
- **`pxa`** — the modern format (0.2.0+). Header: `\x00pxa`, then the
  decompressed length (2 bytes, big-endian) and the compressed length
  (2 bytes, big-endian). The rest is a bitstream **read LSB-first**.

### pxa bitstream (hand-derived, verified against real exports)

Keep a move-to-front (MTF) table of the 256 byte values, `0x00..0xFF`. Until
the decompressed length is reached, read one bit:

- **`1` → literal.** Read a unary count `n` (1-bits until a 0). The MTF index
  is `read(n+4 bits) + 16·(2ⁿ − 1)` — i.e. `n=0` reads 4 bits (0–15), `n=1`
  reads 5 bits (16–47), and so on. Output `mtf[idx]`, then move it to the
  front.
- **`0` → back-reference.** Read the offset width: `1`→(`1`→5 bits / `0`→10
  bits) / `0`→15 bits; `offset = read(width) + 1`. Then the length: start at
  3, read 3-bit groups adding each, continuing while a group is all-ones (7).
  Copy `length` bytes from `offset` back in the output (overlap allowed).

One portability wrinkle: PICO-8 drops the trailing newline on export, and the
Calypsi-built SNES lexer needs the chunk to end in one (the host lexer
tolerates its absence), so `p8png` re-adds it.

## Adding a cart to the build

Anywhere a `.p8` goes, a `.p8.png` works too:

```sh
make -C snes play PLAYCART=path/to/cart.p8.png
make -C snes menu MENUCARTS="a.p8 b.p8.png c.p8"
```

Only ship carts that are yours or explicitly free-licensed — most BBS carts
are CC-BY-NC-SA and must not go in a public release ROM.
