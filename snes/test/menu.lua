-- M8 multi-cart menu harness. Drives the built-in picker end to end:
--   1. wait for the menu ("menu ready" on the mailbox),
--   2. tap A to launch cart 0 (m7_input) -- watch for "launch",
--   3. hold Right and watch m7_input's dot ($7F:4300) climb -- proof the
--      launched cart is running AND live input reaches its btn(),
--   4. hold Select and watch for "to menu" -- proof return-to-menu works
--      (a fresh lua_State was torn down and the loop came back).
--
-- The launch tap is time-boxed then released: run_menu wait_release()s the
-- launch button, so holding it would deadlock. snesWorkRam: $7F -> 0x10000+.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF010
local RING_SIZE = 3552
local PX = 0x14300                 -- m7_input dot x, poked each frame

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function byte(a) return emu.read(a, emu.memType.snesWorkRam) end

local readIndex, linebuf = 0, ""
local sawMenu, sawLaunch, sawToMenu, sawDrew = false, false, false, false

local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = byte(MB_RING + (readIndex % RING_SIZE))
    if b == 10 then
      if #linebuf > 0 and linebuf:byte(1) < 128 then print("OUT " .. linebuf) end
      if linebuf:find("menu ready") then sawMenu = true end
      if linebuf:find("rmDREW") then sawDrew = true end
      if linebuf:find("^launch") then sawLaunch = true end
      if linebuf:find("to menu") then sawToMenu = true end
      linebuf = ""
    else
      linebuf = linebuf .. string.char(b)
    end
    readIndex = (readIndex + 1) % 65536
  end
end

local phase, frames, p1start = 0, 0, 0

emu.addEventCallback(function()
  -- Press the launch button only AFTER the menu has drawn and settled
  -- (dt 60..95): this is the real-user flow -- the initial ~10-frame flip is
  -- long done, the poll loop is fast, so the press is caught at once.
  if phase == 1 and (frames - p1start) >= 60 and (frames - p1start) < 95 then
    emu.setInput({ a = true }, 0)
  elseif phase == 2 then emu.setInput({ right = true }, 0)
  elseif phase == 3 then emu.setInput({ select = true }, 0)
  end
end, emu.eventType.inputPolled)

local function fail(m) print("RESULT FAIL menu " .. m); emu.stop(1) end

emu.addEventCallback(function()
  frames = frames + 1
  drain()
  if word(MB_MAGIC) ~= 0xCAFE then
    if frames > 900 then fail("no-magic") end
    return
  end
  if phase == 0 then
    if sawMenu then phase, p1start = 1, frames; print("PHASE menu-up") end
  elseif phase == 1 then
    if sawLaunch then phase = 2; print("PHASE launched cart 0") end
  elseif phase == 2 then
    local x = byte(PX)
    if x > 74 then phase = 3; print(string.format("PHASE cart live (dot x=%d)", x)) end
  elseif phase == 3 then
    if sawToMenu then
      print("PHASE returned to menu")
      print("RESULT PASS menu (list -> launch m7_input -> input reaches it -> Select returns)")
      emu.stop(0)
    end
  end
  if frames > 8000 then fail(string.format("timeout phase=%d", phase)) end
end, emu.eventType.endFrame)
