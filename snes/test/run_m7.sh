#!/bin/sh
# Run the m7demo ROM headless and decode SHOT lines into test/*.png
# (crib of emu8086's run_test.sh screenshot protocol).
set -e
cd "$(dirname "$0")/.."
MESEN=/Applications/Mesen.app/Contents/MacOS/Mesen
"$MESEN" --testrunner build/m7demo.sfc test/m7demo.lua 2>/dev/null | python3 -c '
import sys
for line in sys.stdin:
    line = line.strip()
    if line.startswith("SHOT "):
        _, name, hexdata = line.split(" ", 2)
        open("test/" + name, "wb").write(bytes.fromhex(hexdata))
        print("wrote test/" + name)
    else:
        print(line)
'
