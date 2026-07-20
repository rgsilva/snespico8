-- M7a live-input harness. Proves the real SNES pad reaches the cart's btn().
-- m7_input.p8 moves a dot 1px/frame per held direction and pokes x,y,color
-- to $7F:4300-4302. We hold right+down+O until the dot has clearly moved
-- that way, then left+up+X until it reverses -- exercising all four d-pad
-- directions and both face-button columns.
--
-- Everything is gated on the loop being LIVE, read as a non-zero decoded
-- mask in p8i_buttons[0] ($7E:61B2): during the multi-second parse the cart
-- hasn't poked x/y/c yet, so those bytes read stale garbage. Once live we
-- both (a) confirm the decoded mask equals the exact expected bits -- a
-- direct check of read_pad -- and (b) watch the dot move -- proof it reaches
-- the cart's btn().  snesWorkRam: $7F:xxxx -> 0x10000+xxxx.
local MB_MAGIC = 0xF000
local PMASK = 0x61B2                       -- $7E:61B2 p8i_buttons[0]
local PX, PY, PC = 0x14300, 0x14301, 0x14302

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function byte(a) return emu.read(a, emu.memType.snesWorkRam) end

-- expected decoded masks: L=1 R=2 U=4 D=8 O=16 X=32
local M_RDO = 0x02 + 0x08 + 0x10          -- right+down+O
local M_LUX = 0x01 + 0x04 + 0x20          -- left+up+X

local phase, frames, saw1, saw2 = 1, 0, false, false

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
  local mask = word(PMASK) & 0xFF
  if mask == 0 then                        -- loop not live yet (parse); wait
    if frames > 3500 then fail("loop never decoded a held pad") end
    return
  end
  local x, y, c = byte(PX), byte(PY), byte(PC)
  if phase == 1 then
    if mask == M_RDO then saw1 = true end  -- read_pad decoded right+down+O
    if x >= 84 and y >= 84 then            -- dot clearly moved +right/+down
      if not saw1 then return fail(string.format("never decoded right+down+O (last mask %02x)", mask)) end
      if c ~= 11 then return fail(string.format("O not applied c=%d (want 11)", c)) end
      print(string.format("PHASE1 ok mask=%02x x=%d y=%d c=%d (right+down+O)", mask, x, y, c))
      phase = 2
    end
  else
    if mask == M_LUX then saw2 = true end  -- read_pad decoded left+up+X
    if x <= 70 and y <= 70 then            -- dot reversed
      if not saw2 then return fail(string.format("never decoded left+up+X (last mask %02x)", mask)) end
      if c ~= 12 then return fail(string.format("X not applied c=%d (want 12)", c)) end
      print(string.format("PHASE2 ok mask=%02x x=%d y=%d c=%d (left+up+X)", mask, x, y, c))
      print("RESULT PASS input (SNES pad -> btn(): 4 dirs + O/X, decode + end to end)")
      emu.stop(0)
    end
  end
  if frames > 6000 then fail(string.format("timeout phase=%d mask=%02x x=%d y=%d", phase, mask, x, y)) end
end, emu.eventType.endFrame)
