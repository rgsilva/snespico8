-- M4 parity harness: drain the whole mailbox ring to stdout (one "OUT
-- <line>" per printh line and per status line), stop when the ROM sets
-- STATUS (PASS/PANIC). The orchestrator (core-c/tests/snesparity.sh)
-- strips the status lines and diffs the remainder against the host core.
local MB_MAGIC, MB_STATUS, MB_WINDEX, MB_RING = 0xF000, 0xF002, 0xF004, 0xF010
local RING_SIZE = 3552
local frames, sawMagic, readIndex, line = 0, false, 0, ""

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end

local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if word(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 600 then print("RESULT FAIL no-magic"); emu.stop(98) end
    return
  end
  drain()
  local status = word(MB_STATUS)
  if status ~= 0 then
    drain()
    if status == 1 then print("RESULT PASS"); emu.stop(0)
    else print(string.format("RESULT FAIL status=%04X", status)); emu.stop(1) end
    return
  end
  if frames > 5400 then drain(); print("RESULT FAIL timeout"); emu.stop(99) end
end, emu.eventType.endFrame)
