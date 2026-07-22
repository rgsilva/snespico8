-- M9 T5: drive the A/B math ROM (-DP8_ABMATH). Echo the mailbox ring,
-- stop on status (1 = ABMATH PASS, 0xDEAD = mismatch); generous frame
-- cap -- the corpus is ~40K hardware-vs-software comparisons and takes
-- a few emulated minutes.
local MAGIC, STATUS, WINDEX, RING = 0xF000, 0xF002, 0xF004, 0xF020
local RING_SIZE = 3536
local frames, readIndex, line = 0, 0, ""

local function drain()
  local wi = emu.read16(WINDEX, emu.memType.snesWorkRam)
  while readIndex ~= wi do
    local b = emu.read(RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

emu.addEventCallback(function()
  frames = frames + 1
  if frames % 60 ~= 0 then return end
  drain()
  local st = emu.read16(STATUS, emu.memType.snesWorkRam)
  if st == 1 then print("RESULT PASS abmath"); emu.stop(0) return end
  if st ~= 0 then print(string.format("RESULT FAIL abmath status=%04X", st)); emu.stop(1) return end
  if frames > 40000 then print("RESULT FAIL abmath timeout"); emu.stop(99) end
end, emu.eventType.endFrame)
