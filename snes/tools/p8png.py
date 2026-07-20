#!/usr/bin/env python3
"""Decode a PICO-8 .p8.png cart into the same (lua, gfx, map, gff, sfx,
music) tuple the .p8 text parser produces, so the build tools accept either
format. This runs entirely on the PC at BUILD TIME -- the SNES never sees a
PNG; the ROM ends up with the already-decoded cart data, exactly as for a
.p8 text cart.

A .p8.png is a 160x205 RGBA image with the 32 KB cart hidden in the low two
bits of each colour channel (steganography). The Lua code (region 0x4300..)
is stored raw or compressed -- the modern 'pxa' format or the old ':c:' one.
The PNG is decoded with zlib only (no Pillow). Verified against a real
PICO-8 export: round-trips to the original source byte for byte."""
import zlib


def _decode_png(path):
    """PNG (8-bit RGBA, colour type 6) -> raw RGBA bytes. Handles the five
    scanline filters; PICO-8 only ever emits 160x205x32bpp."""
    d = open(path, 'rb').read()
    if d[:8] != bytes.fromhex('89504e470d0a1a0a'):
        raise ValueError("%s: not a PNG" % path)
    i, idat, w, h, bd, ct = 8, b'', 0, 0, 0, 0
    while i < len(d):
        ln = int.from_bytes(d[i:i+4], 'big')
        typ = d[i+4:i+8]
        body = d[i+8:i+8+ln]
        if typ == b'IHDR':
            w = int.from_bytes(body[0:4], 'big')
            h = int.from_bytes(body[4:8], 'big')
            bd, ct = body[8], body[9]
        elif typ == b'IDAT':
            idat += body
        elif typ == b'IEND':
            break
        i += 12 + ln
    if (w, h, bd, ct) != (160, 205, 8, 6):
        raise ValueError("%s: not a 160x205 8-bit RGBA .p8.png (got %dx%d bd%d ct%d)"
                         % (path, w, h, bd, ct))
    raw = zlib.decompress(idat)
    bpp, stride = 4, w * 4
    out = bytearray()
    prev = bytearray(stride)
    pos = 0
    for _y in range(h):
        ft = raw[pos]
        pos += 1
        line = bytearray(raw[pos:pos+stride])
        pos += stride
        if ft == 1:              # Sub
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x-bpp]) & 255
        elif ft == 2:            # Up
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 255
        elif ft == 3:            # Average
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 255
        elif ft == 4:            # Paeth
            for x in range(stride):
                a = line[x-bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x-bpp] if x >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p-a), abs(p-b), abs(p-c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 255
        elif ft != 0:
            raise ValueError("%s: bad PNG filter %d" % (path, ft))
        out += line
        prev = line
    return w, h, bytes(out)


def _stego(w, h, rgba):
    """One cart byte per pixel from the low 2 bits: (a<<6)|(r<<4)|(g<<2)|b."""
    data = bytearray(w * h)
    for i in range(w * h):
        r, g, b, a = rgba[i*4], rgba[i*4+1], rgba[i*4+2], rgba[i*4+3]
        data[i] = ((a & 3) << 6) | ((r & 3) << 4) | ((g & 3) << 2) | (b & 3)
    return bytes(data)


def _pxa(block):
    """PICO-8 'pxa' code decompression. Header: '\\x00pxa' + declen(2, BE) +
    complen(2, BE); then a bitstream read LSB-first. A move-to-front table
    codes literals; 0-bits introduce (offset,length) back-references."""
    declen = (block[4] << 8) | block[5]
    pos = [64]                                   # bit index, past the 8-byte header
    nbits = len(block) * 8

    def gb():
        if pos[0] >= nbits:
            return 0
        v = (block[pos[0] >> 3] >> (pos[0] & 7)) & 1
        pos[0] += 1
        return v

    def gbits(k):
        v = 0
        for i in range(k):
            v |= gb() << i
        return v

    mtf = list(range(256))
    out = bytearray()
    while len(out) < declen:
        if gb():                                 # literal, move-to-front coded
            n = 0
            while gb():
                n += 1
            idx = gbits(n + 4) + 16 * ((1 << n) - 1)
            ch = mtf.pop(idx)
            mtf.insert(0, ch)
            out.append(ch)
        else:                                    # back reference
            offbits = (5 if gb() else 10) if gb() else 15
            offset = gbits(offbits) + 1
            length = 3
            while True:
                part = gbits(3)
                length += part
                if part != 7:
                    break
            src = len(out) - offset
            for _ in range(length):
                out.append(out[src])
                src += 1
    return bytes(out[:declen])


def _decompress_code(cart):
    code = cart[0x4300:0x8000]
    if code[:4] == b'\x00pxa':
        return _pxa(code)
    if code[:3] == b':c:':
        raise NotImplementedError(
            "old ':c:' compressed code -- re-save the cart in a recent PICO-8 (pxa format)")
    # uncompressed: raw source, terminated by a NUL (or the region end)
    end = code.find(b'\x00')
    return bytes(code[:end if end >= 0 else len(code)])


def _trim(b):
    """Drop trailing zero bytes (RAM is zero-init, so they cost ROM for
    nothing), matching how cart2c keeps only the meaningful bytes."""
    e = len(b)
    while e > 0 and b[e-1] == 0:
        e -= 1
    return list(b[:e])


def parse_p8png(path):
    """.p8.png -> (lua_bytes, gfx, map, gff, sfx, music) -- same shape as the
    .p8 text parser in cart2c / multicart2c."""
    w, h, rgba = _decode_png(path)
    cart = _stego(w, h, rgba)
    lua = _decompress_code(cart)
    # PICO-8 drops the trailing newline when it exports a cart; the .p8 text
    # loader always keeps one, and the Calypsi-built SNES lexer needs the
    # chunk to end in a newline (the host lexer tolerates its absence). Add
    # it back so a png-loaded cart parses identically to its .p8 form.
    if lua and not lua.endswith(b"\n"):
        lua += b"\n"
    return (lua,
            _trim(cart[0x0000:0x2000]),          # gfx  -> $0000
            _trim(cart[0x2000:0x3000]),          # map  -> $2000
            _trim(cart[0x3000:0x3100]),          # gff  -> $3000
            _trim(cart[0x3200:0x4300]),          # sfx  -> $3200
            _trim(cart[0x3100:0x3200]))          # music-> $3100


if __name__ == "__main__":
    import sys
    lua, gfx, gmap, gff, sfx, music = parse_p8png(sys.argv[1])
    sys.stderr.write("p8png: %s -> lua %dB, gfx %dB, map %dB, gff %dB, sfx %dB, music %dB\n"
                     % (sys.argv[1], len(lua), len(gfx), len(gmap), len(gff), len(sfx), len(music)))
    sys.stdout.buffer.write(lua)
