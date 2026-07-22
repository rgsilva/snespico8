#!/usr/bin/env python3
"""Fold p8prof.lua's PC histogram through the Calypsi link map.

Input: build/p8.map (ln65816 --list-file) and a file containing the
profiler's `PCS <hex-addr> <count>` lines (plus any other harness noise,
which is ignored). The map's symbol blocks look like

    name in section 'farcode'
     placed at address c00000-c05aa8 of size 005aa9

(possibly folded onto one line, which library symbols use). Every PC
sample is attributed to the covering symbol; unmatched PCs are bucketed
by 256-byte page so hot unknown regions still surface.

Usage: profsym.py build/p8.map prof_output.txt
"""
import re
import sys

SYM_RE = re.compile(
    r"(\S+(?: \(from [^)]+\))?) in section '[^']+'\s+placed at address "
    r"([0-9a-f]+)-([0-9a-f]+)", re.S)
PCS_RE = re.compile(r"^PCS ([0-9a-fA-F]+) (\d+)")


def main(mapfile, procfile):
    text = open(mapfile).read()
    syms = []
    for m in SYM_RE.finditer(text):
        name, lo, hi = m.group(1), int(m.group(2), 16), int(m.group(3), 16)
        syms.append((lo, hi, name))
    syms.sort()

    counts = {}
    total = 0
    for line in open(procfile):
        m = PCS_RE.match(line)
        if not m:
            continue
        pc, n = int(m.group(1), 16), int(m.group(2))
        total += n
        name = None
        # linear scan is fine: a few thousand symbols, a few thousand buckets
        for lo, hi, s in syms:
            if lo <= pc <= hi:
                name = s
                break
        if name is None:
            name = "?%06x" % (pc & 0xFFFF00)
        counts[name] = counts.get(name, 0) + n

    if not total:
        print("profsym: no PCS lines found in", procfile)
        return 1
    print("profsym: %d samples, %d symbols hit" % (total, len(counts)))
    cum = 0
    for name, n in sorted(counts.items(), key=lambda kv: -kv[1])[:40]:
        cum += n
        print("%7d  %5.1f%%  %5.1f%%  %s" % (n, 100.0 * n / total,
                                             100.0 * cum / total, name))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1], sys.argv[2]))
