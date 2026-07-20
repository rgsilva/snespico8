-- M8b .p8.png smoke test. The ROM is built from demo_video.p8.png (the cart
-- decoded from a real PICO-8 PNG export at build time), so a correct render
-- proves the whole PNG -> stego -> pxa -> ROM path. demo_video draws the 16
-- colour palette as bars across the top (bar c spans x=c*8..c*8+7, y 0..30),
-- so we check a few bar pixels in the framebuffer at $7F:6000.
local MB_MAGIC, MB_STATUS = 0xF000, 0xF002
local SCREEN = 0x16000

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function px(x, y)
  local b = emu.read(SCREEN + y * 64 + (x >> 1), emu.memType.snesWorkRam)
  if x % 2 == 0 then return b & 0x0F else return (b >> 4) & 0x0F end
end

-- (x, y, expected colour) -- x//8 = colour index of that bar
local checks = { {4, 12, 0}, {12, 12, 1}, {60, 12, 7}, {100, 12, 12}, {124, 12, 15} }
local frames = 0

emu.addEventCallback(function()
  frames = frames + 1
  if word(MB_MAGIC) ~= 0xCAFE then
    if frames > 600 then print("RESULT FAIL png no-magic"); emu.stop(98) end
    return
  end
  if word(MB_STATUS) ~= 0 then
    local ok = (word(MB_STATUS) == 1)
    for _, c in ipairs(checks) do
      local got = px(c[1], c[2])
      if got ~= c[3] then
        print(string.format("PIXEL FAIL (%d,%d) got %d want %d", c[1], c[2], got, c[3]))
        ok = false
      end
    end
    if ok then print("RESULT PASS png (demo_video loaded from .p8.png renders the palette bars)"); emu.stop(0)
    else print("RESULT FAIL png"); emu.stop(1) end
    return
  end
  if frames > 3000 then print("RESULT FAIL png timeout"); emu.stop(99) end
end, emu.eventType.endFrame)
