-- M7a live-input harness. Proves the real SNES pad reaches the cart's btn().
-- m7_input.p8 moves a dot 1px/frame per held direction and pokes x,y,color to
-- $7F:4300-4302 (PICO-8 RAM -- a stable address, unlike a core .bss global,
-- which shifts when code is added). We hold right+down+O until the dot has
-- clearly moved that way, then left+up+X until it reverses -- exercising all
-- four d-pad directions and both face-button columns end to end through
-- read_pad. The dot movement inherently proves the decode (right -> +x, etc).
-- snesWorkRam: $7F:xxxx -> 0x10000+xxxx.
local MB_MAGIC = 0xF000
local PX, PY, PC = 0x14300, 0x14301, 0x14302   -- dot x, y, color

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function byte(a) return emu.read(a, emu.memType.snesWorkRam) end

local phase, frames = 1, 0

emu.addEventCallback(function()
  if phase == 1 then emu.setInput({ right = true, down = true, b = true }, 0)
  else               emu.setInput({ left = true,  up = true,   a = true }, 0) end
end, emu.eventType.inputPolled)

local function fail(m) print("RESULT FAIL input " .. m); emu.stop(1) end

emu.addEventCallback(function()
  frames = frames + 1
  if word(MB_MAGIC) ~= 0xCAFE then
    if frames > 900 then fail("no-magic") end
    return
  end
  -- the dot starts at (64,64) once the cart runs; during the multi-second
  -- parse these bytes read 0 (RAM cleared), so the thresholds can't trip early.
  local x, y, c = byte(PX), byte(PY), byte(PC)
  if phase == 1 then
    if x >= 84 and y >= 84 then                 -- clearly moved +right/+down
      if c ~= 11 then return fail(string.format("O not applied c=%d (want 11)", c)) end
      print(string.format("PHASE1 ok x=%d y=%d c=%d (right+down+O)", x, y, c))
      phase = 2
    end
  else
    if x <= 70 and y <= 70 then                 -- reversed +left/+up
      if c ~= 12 then return fail(string.format("X not applied c=%d (want 12)", c)) end
      print(string.format("PHASE2 ok x=%d y=%d c=%d (left+up+X)", x, y, c))
      print("RESULT PASS input (SNES pad -> btn(): 4 dirs + O/X, end to end)")
      emu.stop(0)
    end
  end
  if frames > 6000 then fail(string.format("timeout phase=%d x=%d y=%d c=%d", phase, x, y, c)) end
end, emu.eventType.endFrame)
