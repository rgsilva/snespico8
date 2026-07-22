-- M6a/b SPC test. The ROM (built -DP8_SPC_TEST) uploads the driver and
-- keys four voices with four PICO-8 waveforms at four known pitches, then
-- holds. This harness checks two things:
--   (a) upload + transport: the driver's voice mask at SPC $00 goes to
--       0x0F (voices 0-3 keyed), proving the whole audio path end to end;
--   (b) in tune: the DSP pitch P the driver programmed (stashed at
--       $30+voice*2) matches pitch_tab = round(440*2^((pitch-33)/12)*4.096)
--       for each note -- i.e. the notes are at the right frequency.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF020
local RING_SIZE = 3536
-- voices 0..3 were keyed with these PICO-8 pitches:
local PITCH = {24, 36, 45, 52}
local frames, sawMagic, readIndex, line = 0, false, 0, ""
local nz, last = 0, -1

local function w16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function spc(a) return emu.read(a, emu.memType.spcMemory) end
local function spc16(a) return spc(a) + spc(a + 1) * 256 end

local function expectedP(p)
  local f = 440.0 * 2 ^ ((p - 33) / 12)
  local P = math.floor(f * 4.096 + 0.5)
  return math.max(1, math.min(0x3FFF, P))
end

local function drain()
  local wi = w16(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

local function check_tuning()
  local ok = true
  for v = 0, 3 do
    local got = spc16(0x30 + v * 2)
    local want = expectedP(PITCH[v + 1])
    local d = math.abs(got - want)
    print(string.format("voice %d pitch %d: P got=%d want=%d %s",
      v, PITCH[v + 1], got, want, d <= 1 and "OK" or "MISTUNED"))
    if d > 1 then ok = false end
  end
  return ok
end

emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if w16(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 600 then print("RESULT FAIL spc no-magic"); emu.stop(1) end
    return
  end
  drain()
  local m = spc(0)                         -- driver dp $00 = voice mask
  if m ~= 0 then nz = nz + 1 end
  last = m
  if frames % 400 == 0 then
    print(string.format("f=%d spc00=%02X nz=%d", frames, m, nz))
  end
  if frames > 1200 then
    drain()
    local tuned = check_tuning()
    print(string.format("voicemask final=%02X nz=%d", last, nz))
    if last == 0x0F and nz > 20 and tuned then
      print("RESULT PASS spc (4 voices keyed, all in tune)")
      emu.stop(0)
    else
      print("RESULT FAIL spc (mask/tuning check failed)")
      emu.stop(1)
    end
  end
end, emu.eventType.endFrame)
