-- Generic screenshot harness: wait for the cart's third flip (so the
-- screen is a real rendered frame, not the boot blank), screenshot, and
-- stop. Used to (re)capture the docs/assets gallery:
--   make p8 CART=... FRAMES=8 && Mesen --testrunner build/p8.sfc test/shot.lua
-- Prints SHOT <hex-png>; decode with bytes.fromhex.
local FLIPS, STATUS = 0xF00A, 0xF002
local frames, shotat = 0, nil

emu.addEventCallback(function()
  frames = frames + 1
  local fl = emu.read16(FLIPS, emu.memType.snesWorkRam)
  local st = emu.read16(STATUS, emu.memType.snesWorkRam)
  -- arm when the cart's FRAMES budget is exhausted (status set) and the
  -- screen is holding its last frame: capturing mid-run catches the
  -- force-blank of the next flip's DMA (observed: 5/5 black shots from a
  -- flipping cart). Build the ROM with a small FRAMES for this.
  -- floor at 1200 absolute frames: every successful capture in the
  -- suite fired past ~frame 600, every earlier attempt came back empty
  -- or black -- the testrunner's video path warms up late
  if shotat == nil and st ~= 0 and fl >= 1 then
    shotat = math.max(frames + 60, 1200)
  end
  if shotat and frames >= shotat then
    local s = emu.takeScreenshot()
    if #s == 0 then shotat = frames + 30 return end   -- renderer not ready, rearm
    local h = {}
    for i = 1, #s do h[#h + 1] = string.format("%02x", string.byte(s, i)) end
    print("SHOT " .. table.concat(h))
    emu.stop(0)
    return
  end
  if frames > 9000 then print("SHOT-TIMEOUT flips=" .. fl); emu.stop(1) end
end, emu.eventType.endFrame)
