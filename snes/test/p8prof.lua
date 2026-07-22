-- M9 T0 PC-sampling profiler: one emu.getState() program-counter sample per
-- frame (the micropython-snes technique), dumped as a histogram of
--   PCS <k:pc hex> <count>
-- lines for tools/profsym.py to fold through build/p8.map. Sampling at
-- frame boundaries is unbiased here because game frames span hundreds of
-- video frames -- nothing is frame-locked to the sampler.
local N = 5000
local hist, frames = {}, 0

emu.addEventCallback(function()
  frames = frames + 1
  local st = emu.getState()
  local pc = st["cpu.k"] and (st["cpu.k"] * 65536 + st["cpu.pc"]) or nil
  if pc == nil then
    -- field-name probe: dump what the state table actually calls the PC
    print("PROF no cpu.k/cpu.pc; state keys:")
    for k, _ in pairs(st) do
      if string.find(k, "pc") or string.find(k, "cpu") then print("  " .. k) end
    end
    emu.stop(1)
    return
  end
  hist[pc] = (hist[pc] or 0) + 1
  if frames >= N then
    for a, c in pairs(hist) do print(string.format("PCS %06x %d", a, c)) end
    print("PROF done frames=" .. frames)
    emu.stop(0)
  end
end, emu.eventType.endFrame)
