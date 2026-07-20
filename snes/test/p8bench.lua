local VMOPS = 0x615F
local frames = 0
local function ops() return emu.read32(VMOPS, emu.memType.snesWorkRam) end
local last = 0
emu.addEventCallback(function()
  frames = frames + 1
  if frames % 60 == 0 then
    local o = ops()
    print(string.format("f=%d vmops=%d", frames, o))
    if o == last and o > 100 then print("PLATEAU vmops="..o.." frame="..frames); emu.stop(0) end
    last = o
  end
  if frames > 5000 then print("TIMEOUT vmops="..ops()); emu.stop(0) end
end, emu.eventType.endFrame)
