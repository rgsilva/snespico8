-- M6d end-to-end test. This is a NORMAL ROM: the cart's Lua runs, its
-- _init calls music(0), and the core routes that to the SPC. Nothing here
-- pokes the driver directly -- if the SPC ends up playing and chaining
-- patterns, the whole path (cart Lua -> core sfx()/music() -> mailbox ->
-- SPC sequencer) works. The game loop finishes quickly (a few frames) but
-- the SPC keeps playing on its own, so we sample it well past that.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF010
local RING_SIZE = 3552
local frames, sawMagic, readIndex, line = 0, false, 0, ""
local patset, masknz = {}, false

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

emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if wram16(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 900 then print("RESULT FAIL audio no-magic"); emu.stop(1) end
    return
  end
  drain()
  local pat = spc(0x60)                       -- mus_pat
  if pat ~= 0xFF then patset[pat] = true end
  if spc(0) ~= 0 then masknz = true end       -- some voice keyed
  if frames > 1500 then
    drain()
    local npat = 0
    for _ in pairs(patset) do npat = npat + 1 end
    print(string.format("patterns=%d masknz=%s mus_pat=%02X mask=%02X",
      npat, tostring(masknz), spc(0x60), spc(0)))
    if npat >= 2 and masknz then
      print("RESULT PASS audio (cart music() reaches the SPC; song plays + chains)")
      emu.stop(0)
    else
      print("RESULT FAIL audio (music/keying not observed on the SPC)")
      emu.stop(1)
    end
  end
end, emu.eventType.endFrame)
