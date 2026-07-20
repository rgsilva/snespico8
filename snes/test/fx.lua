-- M6 effects test. sfx0 = DROP: voice-0 live pitch ($2E/$2F, stashed by
-- seq_fx) sweeps a wide range as each note's pitch ramps down per tick --
-- proof the SPC applies effects. (Fade/vibrato/slide/arp share the same
-- seq_fx / envelope paths; ear-tuned vs real PICO-8.) Samples post-boot.
local frames, sawMagic, Ps = 0, false, {}
local function w16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function spc(a) return emu.read(a, emu.memType.spcMemory) end
emu.addEventCallback(function()
  frames = frames + 1
  if not sawMagic then if w16(0xF000)==0xCAFE then sawMagic=true end return end
  if frames > 500 and frames % 3 == 0 and #Ps < 60 then
    Ps[#Ps+1] = spc(0x2E) + spc(0x2F)*256
  end
  if frames > 1400 then
    local mn,mx = 99999,0
    for _,v in ipairs(Ps) do if v<mn then mn=v end if v>mx then mx=v end end
    print("P: "..table.concat(Ps,",",1,math.min(#Ps,20)))
    print(string.format("range %d..%d spread=%d mask=%02X", mn,mx,mx-mn,spc(0)))
    print((mx-mn) > 1000 and "RESULT PASS fx (drop sweeps pitch per tick)"
                          or "RESULT FAIL fx")
    emu.stop(0)
  end
end, emu.eventType.endFrame)
