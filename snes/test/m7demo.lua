-- M0d harness: waits for the $CAFE magic, spot-checks pixels straight in
-- VRAM (Mode 7 tile data = high bytes), prints the expansion/DMA frame
-- benchmark, and dumps a screenshot as hex (SHOT protocol, decoded by
-- run_m7.sh).
local frames = 0

-- pixel (x,y) -> VRAM byte index of its Mode 7 tile-data byte
local function vramAddr(x, y)
  local tile = (math.floor(y / 8) * 16) + math.floor(x / 8)
  local off = (y % 8) * 8 + (x % 8)
  return (tile * 64 + off) * 2 + 1 -- high byte of the word
end

-- {x, y, expected color}: stripes = x>>3, diagonal (i,i) = 7
local checks = {
  { 12, 50, 1 },
  { 100, 30, 12 },
  { 60, 90, 7 },
  { 51, 51, 7 },   -- diagonal
  { 0, 127, 0 },   -- stripe 0 bottom-left
  { 127, 0, 15 },  -- stripe 15 top-right
}

local function word(addr)
  return emu.readWord(addr, emu.memType.snesWorkRam, false)
end

local function onFrame()
  frames = frames + 1
  if word(0x10) == 0xCAFE then
    local c0, c1, c2 = word(0x12), word(0x14), word(0x16)
    print(string.format("BENCH expand8 %d frames, dma8 %d frames", c1 - c0, c2 - c1))
    local ok = true
    for _, c in ipairs(checks) do
      local got = emu.read(vramAddr(c[1], c[2]), emu.memType.snesVideoRam)
      if got ~= c[3] then
        print(string.format("PIXEL FAIL (%d,%d) got %d want %d", c[1], c[2], got, c[3]))
        ok = false
      end
    end
    -- let a few frames render with the screen on, then screenshot
    if frames < 300 then
      return
    end
    local shot = emu.takeScreenshot()
    local hex = {}
    for i = 1, #shot do
      hex[#hex + 1] = string.format("%02x", string.byte(shot, i))
    end
    print("SHOT m7demo.png " .. table.concat(hex))
    if ok then
      print("RESULT PASS m7demo")
      emu.stop(0)
    else
      print("RESULT FAIL m7demo pixels")
      emu.stop(1)
    end
    return
  end
  if frames > 1800 then
    print("RESULT FAIL m7demo timeout")
    emu.stop(99)
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
