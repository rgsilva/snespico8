-- M6a APU bring-up test. The ROM (built -DP8_SPC_TEST) uploads the SPC
-- driver, keys three voices, and holds. This harness drains the boot
-- mailbox and samples the driver's voice-mask at SPC $00: it goes non-zero
-- only if the IPL upload landed, the $2140-$2143 transport works, and the
-- driver processed the KON commands (issuing DSP key-ons). That is the
-- whole audio path proven end to end.
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF010
local RING_SIZE = 3552
local frames, sawMagic, readIndex, line = 0, false, 0, ""
local nz, changes, last, samples = 0, 0, -1, {}

local function w16(a) return emu.read16(a, emu.memType.snesWorkRam) end

local function drain()
  local wi = w16(MB_WINDEX)
  while readIndex ~= wi do
    local b = emu.read(MB_RING + (readIndex % RING_SIZE), emu.memType.snesWorkRam)
    if b == 10 then print("OUT " .. line); line = ""
    else line = line .. string.char(b) end
    readIndex = (readIndex + 1) % 65536
  end
end

emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then
    if w16(MB_MAGIC) == 0xCAFE then sawMagic = true
    elseif frames > 600 then print("RESULT FAIL spc no-magic"); emu.stop(1) end
    return
  end
  drain()
  local m = emu.read(0, emu.memType.spcMemory)   -- driver dp $00 = voice mask
  if m ~= 0 then nz = nz + 1 end
  if m ~= last then
    changes = changes + 1; last = m
    if #samples < 32 then samples[#samples + 1] = string.format("%02X", m) end
  end
  if frames % 300 == 0 then
    print(string.format("f=%d spc00 nz=%d changes=%d mask=%02X [%s]",
      frames, nz, changes, last, table.concat(samples, " ")))
  end
  if frames > 1500 then
    drain()
    print(string.format("voicemask nz=%d changes=%d final=%02X", nz, changes, last))
    if nz > 20 and last ~= 0 then
      print("RESULT PASS spc (driver uploaded + keyed voices)")
      emu.stop(0)
    else
      print("RESULT FAIL spc (voice mask stayed 0 - upload/transport broken)")
      emu.stop(1)
    end
  end
end, emu.eventType.endFrame)
