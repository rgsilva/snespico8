#!/usr/bin/env python3
"""Codegen fences: scan Calypsi .lst files for known-miscompile signatures.

Run against build/p8obj before every link (the Makefile wires it into the
p8.raw rule). Two fences, both from bugs Calypsi has actually shipped here
or in micropython-snes:

1. Negative-Y far indexing (all objects): `ldy ##-n` followed by
   `lda/sta [dp],y`. The 65816 adds Y as UNSIGNED 16-bit to the 24-bit
   base, so a "negative" index carries into the bank byte and hits the
   next bank. Port of micropython-snes tools/check_neg_index.py (which
   also warns on its ora-null-test A-clobber pattern).

2. Bank-$7E high-half constants (core-c objects only): any literal
   `long:0x7e8000-0x7effff` operand. core-c code only ever addresses
   PICO-8 RAM (bank $7F), the SRAM/WRAM heap (via runtime pointers) and
   the one MB_VMOPS mailbox slot -- a constant-folded $7E:8000+ access is
   the bugs/calypsi-const-ptr-offset.c sign-extension misfold resurfacing
   (a $7F pointer plus a "negative" 16-bit offset folded into bank $7E).
   snes-side objects (p8main, mailbox, stubs, p8spc) legitimately use the
   $7E:8000 M7 staging buffer and the $7E:F0xx mailbox, so they are
   exempt from this rule (rule 1 still applies).
   Limitation: only the absolute-long operand form is fenced; a misfolded
   pointer materialized via 16-bit immediate halves would not match.

Exit 1 on any fatal hit; loud failure if no .lst files are found (a fence
that scans nothing must not pass).
"""
import os
import re
import sys

LDY_RE = re.compile(r"\bldy\s+##(-\d+|0x[fF][0-9a-fA-F]{3})\b")
LONG_IDX_RE = re.compile(r"\b(lda|sta)\s+\[[^]]+\],y")
FUNC_RE = re.compile(r"^\s*\\ [0-9a-f]{6}\s+(\S+):")
ORA_RE = re.compile(r"\bora\s+dp:")
A_STORE_RE = re.compile(r"\bsta\s+")
NEUTRAL_RE = re.compile(r"\b(beq|bne|bra|stx|sty|phx|phy|ldy|ldx|cpx|cpy|rep|sep)\b")

# bank $7E, offset >= 0x8000, as a literal absolute-long operand
BANK7E_RE = re.compile(r"long:0x7e([89a-f][0-9a-f]{3})\b")
# the one address core-c legitimately owns: MB_VMOPS ($7E:F00C, 32-bit ->
# the high word is accessed at +2)
BANK7E_ALLOW = {0xF00C, 0xF00E}

# objects allowed to touch $7E:8000+ by design (M7 buffer, mailbox)
BANK7E_EXEMPT = {"p8main.lst", "mailbox.lst", "stubs.lst", "p8spc.lst"}


def scan(path, fence_bank7e):
    hits = []
    func = "?"
    pending = None
    ora_line = None
    ora_branched = False
    for lineno, line in enumerate(open(path), 1):
        m = FUNC_RE.match(line)
        if m and not m.group(1).startswith("`?L"):
            func = m.group(1)
        is_code = re.search(r"\\ [0-9a-f]{6} [0-9a-f]", line) is not None

        if fence_bank7e and is_code:
            m = BANK7E_RE.search(line)
            if m and int(m.group(1), 16) not in BANK7E_ALLOW:
                hits.append(("fatal", func, lineno,
                             "bank-$7E constant in core-c: " + line.strip()))

        # ora-null-test A-clobber (warning only, needs a branch in between)
        if is_code:
            if ORA_RE.search(line):
                ora_line = lineno
                ora_branched = False
            elif ora_line is not None:
                if re.search(r"\b(beq|bne)\b", line):
                    ora_branched = True
                elif A_STORE_RE.search(line):
                    if ora_branched:
                        hits.append(("warn", func, ora_line,
                                     "A stored after ora null-test: " + line.strip()))
                    ora_line = None
                elif NEUTRAL_RE.search(line):
                    pass
                else:
                    ora_line = None

        m = LDY_RE.search(line)
        if m:
            iv = int(m.group(1), 0) & 0xFFFF
            pending = (lineno, iv) if iv >= 0x8000 else None
            continue
        if pending and LONG_IDX_RE.search(line):
            hits.append(("fatal", func, pending[0],
                         "negative-Y far index (y=0x%04x): %s"
                         % (pending[1], line.strip())))
            pending = None
        elif line.strip() and "\\" in line:
            if is_code:
                pending = None
    return hits


def main(argv):
    if len(argv) != 1 or not os.path.isdir(argv[0]):
        print("usage: lstcheck.py <dir-with-.lst>")
        return 2
    lsts = sorted(f for f in os.listdir(argv[0]) if f.endswith(".lst"))
    if not lsts:
        print("lstcheck: NO .lst files in %s -- fence scanned nothing" % argv[0])
        return 1
    fatal = warn = 0
    for name in lsts:
        fence7e = name not in BANK7E_EXEMPT
        for kind, func, ln, txt in scan(os.path.join(argv[0], name), fence7e):
            print("%s:%d: %s in %s: %s"
                  % (name, ln, "FATAL" if kind == "fatal" else "WARN", func, txt))
            if kind == "fatal":
                fatal += 1
            else:
                warn += 1
    if fatal:
        print("lstcheck: %d fatal, %d warning(s) across %d listings"
              % (fatal, warn, len(lsts)))
        return 1
    print("lstcheck: clean (%d listings%s)"
          % (len(lsts), ", %d warning(s)" % warn if warn else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
