-- M0a harness: polls the WRAM mailbox ($7EF000, see calypsi/mailbox.h),
-- drains the stdout ring to Mesen's stdout via print() (no io access
-- needed), and exits with the ROM's status word. emu8086-style stdout
-- protocol: "OUT <line>" per mailbox line, one final "RESULT PASS/FAIL".
local MB_MAGIC, MB_STATUS, MB_WINDEX, MB_RING = 0xF000, 0xF002, 0xF004, 0xF010
local RING_SIZE = 3552

local frames = 0
local sawMagic = false
local readIndex = 0
local line = ""

local function word(addr)
  return emu.readWord(addr, emu.memType.snesWorkRam, false)
end

local function flushPart()
  if line ~= "" then
    print("PART " .. line)
    line = ""
  end
end

local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then
      print("OUT " .. line)
      line = ""
    else
      line = line .. string.char(b)
    end
    readIndex = (readIndex + 1) % 65536
  end
end

local function onFrame()
  frames = frames + 1
  if not sawMagic then
    if word(MB_MAGIC) == 0xCAFE then
      sawMagic = true
    elseif frames > 600 then
      print("RESULT FAIL hello no-magic")
      emu.stop(98)
    end
    return
  end
  drain()
  local status = word(MB_STATUS)
  if status ~= 0 then
    drain()
    flushPart()
    if status == 1 then
      print("RESULT PASS hello")
      emu.stop(0)
    else
      print(string.format("RESULT FAIL hello status=%04X", status))
      emu.stop(1)
    end
    return
  end
  if frames % 300 == 0 then
    flushPart()
  end
  if frames > 1800 then
    flushPart()
    print("RESULT FAIL hello timeout")
    emu.stop(99)
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
