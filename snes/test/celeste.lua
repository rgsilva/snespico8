-- M5c: boot Celeste on the SNES. Tracks progress (mailbox ring) and the
-- p8heap telemetry (peak/max_fail from their linked WRAM addresses),
-- prints periodic status, and screenshots when the title flips. No pixel
-- asserts (this is a real cart) — the goal is: does it parse, init, and
-- draw, and does the heap prediction (max_fail=0) hold on real hardware.
local MB_MAGIC, MB_STATUS, MB_WINDEX, MB_FLIPS, MB_RING = 0xF000, 0xF002, 0xF004, 0xF00A, 0xF020
local RING_SIZE = 3536
-- p8heap counters + vmops at their FIXED mailbox slots (mailbox.h) --
-- the old version read .bss offsets copied from a stale build/p8.map
-- and printed garbage after any relink (the M9 drift-trap class)
local PEAK, MAX_OK, MAX_FAIL, VMOPS = 0xF010, 0xF014, 0xF018, 0xF00C

local frames, sawMagic, readIndex, line, shots = 0, false, 0, "", 0
local function w16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function w32(a) return emu.read32(a, emu.memType.snesWorkRam) end

local function drain()
  local wi = w16(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

local function shot(name)
  local s = emu.takeScreenshot()
  local hex = {}
  for i = 1, #s do hex[#hex + 1] = string.format("%02x", string.byte(s, i)) end
  print("SHOT " .. name .. " " .. table.concat(hex))
end

local function status()
  print(string.format("PROG f=%d flips=%d vmops=%d peak=%d maxok=%d maxfail=%d",
    frames, w16(MB_FLIPS), w32(VMOPS), w32(PEAK), w32(MAX_OK), w32(MAX_FAIL)))
end

local lastFlips = 0
local function onFrame()
  frames = frames + 1
  if not sawMagic then
    if w16(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 900 then print("RESULT FAIL celeste no-magic"); emu.stop(98) end
    return
  end
  drain()
  if frames % 600 == 0 then status() end
  -- screenshot the first couple of flips (title screen)
  local fl = w16(MB_FLIPS)
  if fl > lastFlips and shots < 2 then
    lastFlips = fl
    shots = shots + 1
    drain()
    status()
    shot("celeste_snes.png")
  end
  local st = w16(MB_STATUS)
  if st ~= 0 then
    drain(); status()
    if st == 1 then print("RESULT PASS celeste (booted)"); shot("celeste_snes.png"); emu.stop(0)
    else print(string.format("RESULT FAIL celeste status=%04X", st)); emu.stop(1) end
    return
  end
  if frames > 20000 then  -- ride Mesen's wall limit; report how far it got
    drain(); status()
    print("RESULT PARTIAL celeste timeout")
    if lastFlips > 0 then shot("celeste_snes.png") end
    emu.stop(0)
  end
end

emu.addEventCallback(onFrame, emu.eventType.endFrame)
