-- M3 boot harness. Mailbox at $7E:F000 (see calypsi/mailbox.h): drain
-- printh output, read flip/vmops telemetry, and — the real proof —
-- verify the PICO-8 framebuffer the compiled core drew into $7F:6000
-- (WRAM), independent of the Mode 7 video path. Then screenshot.
--
-- snesWorkRam offsets are relative to $7E:0000, so:
--   mailbox $7E:F000  -> 0xF000
--   pico screen $7F:6000 -> 0x16000
local MB_MAGIC, MB_STATUS = 0xF000, 0xF002
local MB_WINDEX, MB_FLIPS, MB_VMOPS, MB_RING = 0xF004, 0xF00A, 0xF00C, 0xF020
local RING_SIZE = 3536
local SCREEN = 0x16000

local frames, sawMagic, readIndex, line = 0, false, 0, ""

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function long(a)
  return word(a) + word(a + 2) * 65536
end

-- PICO-8 framebuffer pixel (x,y) -> color 0..15
local function px(x, y)
  local b = emu.read(SCREEN + y * 64 + (x >> 1), emu.memType.snesWorkRam)
  if x % 2 == 0 then return b & 0x0F else return (b >> 4) & 0x0F end
end

-- m3_boot: 4 color quadrants + diagonals + circle (ground truth from
-- the host oracle, tests/carts/m3_boot.p8)
local checks = {
  { 32, 20, 8 }, { 96, 20, 11 }, { 32, 100, 3 }, { 96, 100, 14 },
  { 20, 20, 7 }, { 107, 20, 10 }, { 64, 10, 11 }, { 10, 64, 3 },
}

local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

local function screenshot()
  local shot = emu.takeScreenshot()
  local hex = {}
  for i = 1, #shot do hex[#hex + 1] = string.format("%02x", string.byte(shot, i)) end
  print("SHOT p8boot.png " .. table.concat(hex))
end

local function onFrame()
  frames = frames + 1
  if not sawMagic then
    if word(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 600 then print("RESULT FAIL p8boot no-magic"); emu.stop(98) end
    return
  end
  drain()
  local status = word(MB_STATUS)
  if status ~= 0 then
    drain()
    print(string.format("TELEM flips=%d vmops=%d", word(MB_FLIPS), long(MB_VMOPS)))
    local ok = (status == 1)
    for _, c in ipairs(checks) do
      local got = px(c[1], c[2])
      if got ~= c[3] then
        print(string.format("PIXEL FAIL (%d,%d) got %d want %d", c[1], c[2], got, c[3]))
        ok = false
      end
    end
    screenshot()
    if ok then print("RESULT PASS p8boot"); emu.stop(0)
    else print("RESULT FAIL p8boot"); emu.stop(1) end
    return
  end
  if frames > 3000 then  -- generous: interpreter on 65816 is slow
    drain()
    print("RESULT FAIL p8boot timeout")
    emu.stop(99)
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
