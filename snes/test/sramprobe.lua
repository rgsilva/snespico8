-- Mesen2 testrunner script for sramprobe.asm.
-- Polls WRAM $06 for the $CAFE done flag, then prints the distinct-bank
-- count and the raw per-bank readbacks ($10 + i*2, banks $70..$7D).
local frames = 0

local function onFrame()
  frames = frames + 1
  local done = emu.read16(0x06, emu.memType.snesWorkRam)
  if done == 0xCAFE then
    local count = emu.read16(0x04, emu.memType.snesWorkRam)
    local banks = {}
    for i = 0, 13 do
      banks[#banks + 1] = string.format("%04X",
        emu.read16(0x10 + i * 2, emu.memType.snesWorkRam))
    end
    print("BANKS " .. table.concat(banks, " "))
    print(string.format("COUNT %d KB %d", count, count * 32))
    print("RESULT PASS sramprobe")
    emu.stop(0)
  elseif frames > 600 then
    print("RESULT FAIL sramprobe timeout")
    emu.stop(1)
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
