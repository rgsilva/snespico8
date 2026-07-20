-- M8 pause-menu harness. m8_pause.p8 registers two menuitem() callbacks that
-- poke a marker to $7F:4300. We: wait for the cart to run, hold Start until
-- the ROM prints "PAUSE" (run_pause opened), release it, navigate down to the
-- first custom item ("say hi"), and select it -- then check 0x4300 == 11,
-- proving Start -> pause menu -> select -> the Lua callback ran.
--
-- Timing note: run_pause's draw+flip briefly blocks its input poll (like the
-- cart menu), so the taps are spaced well past each redraw.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF010
local RING_SIZE = 3552
local PFIRED = 0x14300                 -- $7F:4300 callback marker

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function byte(a) return emu.read(a, emu.memType.snesWorkRam) end

local readIndex, linebuf = 0, ""
local sawInit, sawPause = false, false
local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = byte(MB_RING + (readIndex % RING_SIZE))
    if b == 10 then
      if #linebuf > 0 and linebuf:byte(1) < 128 then print("OUT " .. linebuf) end
      if linebuf:find("init ok") then sawInit = true end
      if linebuf:find("PAUSE") then sawPause = true end
      linebuf = ""
    else
      linebuf = linebuf .. string.char(b)
    end
    readIndex = (readIndex + 1) % 65536
  end
end

local phase, frames, pmark = 0, 0, 0

emu.addEventCallback(function()
  if phase == 1 and not sawPause then
    emu.setInput({ start = true }, 0)        -- hold Start to open the pause menu
  elseif phase == 2 then
    local dt = frames - pmark
    if dt >= 40 and dt < 90 then emu.setInput({ down = true }, 0)    -- -> "say hi"
    elseif dt >= 120 and dt < 140 then emu.setInput({ a = true }, 0) -- select
    end
  end
end, emu.eventType.inputPolled)

local function fail(m) print("RESULT FAIL pause " .. m); emu.stop(1) end

emu.addEventCallback(function()
  frames = frames + 1
  drain()
  if word(MB_MAGIC) ~= 0xCAFE then
    if frames > 900 then fail("no-magic") end
    return
  end
  if phase == 0 then
    if sawInit then phase = 1; print("PHASE cart running") end
  elseif phase == 1 then
    if sawPause then phase, pmark = 2, frames; print("PHASE pause opened") end
  elseif phase == 2 then
    if frames - pmark > 165 then
      local m = byte(PFIRED)
      if m == 11 then
        print("RESULT PASS pause (Start -> menu -> select ran the menuitem callback)")
        emu.stop(0)
      else
        fail(string.format("callback marker=%d (want 11)", m))
      end
    end
  end
  if frames > 6000 then fail("timeout phase=" .. phase) end
end, emu.eventType.endFrame)
