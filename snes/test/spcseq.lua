-- M6c autonomous-sequencer test. The ROM (built -DP8_SPC_SEQ) streams the
-- cart's SFX/music into the SPC, then plays sfx 0 on channel 0 and holds.
-- Everything after that happens on the SPC in real time, driven by its own
-- 121 Hz timer -- the 65816 sends nothing more. This harness checks:
--   (1) streaming: SPC $1000.. == the cart's SFX data at $7F:3200;
--   (2) timer: the driver's heartbeat ($6B/$6C) advances on its own;
--   (3) sequencing: channel 0's row ($44) walks forward and the programmed
--       pitch ($30) changes -- notes are being keyed autonomously.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF020
local RING_SIZE = 3536
local SFX_WRAM = 0x13200            -- $7F:3200 as a snesWorkRam offset
local frames, sawMagic, readIndex, line = 0, false, 0, ""
local hb0, maxrow, pset, patset, masknz = nil, 0, {}, {}, false

local function wram16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function wram(a) return emu.read(a, emu.memType.snesWorkRam) end
local function spc(a) return emu.read(a, emu.memType.spcMemory) end

local function drain()
  local wi = wram16(MB_WINDEX)
  while readIndex ~= wi do
    local b = wram(MB_RING + (readIndex % RING_SIZE))
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

local function ndistinct(t)
  local n = 0
  for _ in pairs(t) do n = n + 1 end
  return n
end

emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if wram16(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 900 then print("RESULT FAIL seq no-magic"); emu.stop(1) end
    return
  end
  drain()
  local row = spc(0x44)                       -- ch_row[0]
  if row > maxrow and row <= 40 then maxrow = row end
  pset[spc(0x30) + spc(0x31) * 256] = true    -- voice 0 programmed pitch P
  local pat = spc(0x60)                        -- mus_pat
  if pat ~= 0xFF then patset[pat] = true end   -- patterns the player visited
  if spc(0) ~= 0 then masknz = true end        -- some voice was keyed
  local hb = spc(0x6B) + spc(0x6C) * 256
  if hb0 == nil then hb0 = hb end

  if frames > 1200 then
    drain()
    local streamok = true
    for i = 0, 31 do
      if spc(0x1000 + i) ~= wram(SFX_WRAM + i) then streamok = false break end
    end
    local np, npat = ndistinct(pset), ndistinct(patset)
    local hbgrew = hb ~= hb0
    print(string.format("stream=%s maxrow=%d distinctP=%d patterns=%d hb %d->%d masknz=%s",
      tostring(streamok), maxrow, np, npat, hb0, hb, tostring(masknz)))
    if streamok and hbgrew and npat >= 2 and masknz and maxrow >= 3 then
      print("RESULT PASS spcseq (SPC plays the song, chaining patterns, in real time)")
      emu.stop(0)
    else
      print("RESULT FAIL spcseq (stream/timer/music-chain/keying check failed)")
      emu.stop(1)
    end
  end
end, emu.eventType.endFrame)
