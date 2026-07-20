-- Probe: is the DSP actually emitting audio? Reads the driver's ENVX
-- readback ($38+v = live envelope of voice v) plus master vol / FLG.
local frames, sawMagic = 0, false
local maxenv = {0,0,0,0}
local function wram16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function spc(a) return emu.read(a, emu.memType.spcMemory) end
emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if wram16(0xF000) == 0xCAFE then sawMagic = true
    elseif frames > 900 then print("FAIL no-magic"); emu.stop(1) end
    return
  end
  for v = 0, 3 do local e = spc(0x38 + v); if e > maxenv[v+1] then maxenv[v+1] = e end end
  if frames % 300 == 0 then
    print(string.format("f=%d mask=%02X mus_pat=%02X env=%d,%d,%d,%d",
      frames, spc(0), spc(0x60), spc(0x38), spc(0x39), spc(0x3A), spc(0x3B)))
  end
  if frames > 1200 then
    print(string.format("MAX ENVX per voice = %d,%d,%d,%d",
      maxenv[1], maxenv[2], maxenv[3], maxenv[4]))
    local any = maxenv[1]+maxenv[2]+maxenv[3]+maxenv[4]
    print(any > 0 and "RESULT PASS audio-output (DSP envelope is live -> real sound)"
                   or "RESULT FAIL audio-output (envelope stayed 0 -> silent)")
    emu.stop(any > 0 and 0 or 1)
  end
end, emu.eventType.endFrame)
