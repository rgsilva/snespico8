-- M9 T0 bench harness. Samples the LIVE VM-op counter once per 60 frames:
-- lvm.c counts straight into the fixed MB_VMOPS mailbox slot ($7E:F00C,
-- calypsi/mailbox.h, pinned by P8_VMOPS_ADDR), so mid-_init compute slopes
-- are visible with zero flips and no link-layout drift (the old version of
-- this script read the p8_vmops .bss address, which had already moved).
-- Prints one line per window; a window is 60 frames ~= 1 second of
-- emulated NTSC time (60/60.0988 = 0.2% short -- ignored).
--   B f=<frame> ops=<total> dops=<window> flips=<total> dflips=<window>
-- Stops on: cart panic, all-frames-done PASS, compute plateau (dops
-- collapses to <5% of peak after real activity), or the frame cap.
local MAGIC, STATUS, FLIPS, VMOPS = 0xF000, 0xF002, 0xF00A, 0xF00C
-- CAP fits every bench cart; celeste's ~59 s parse + title needs ~12000,
-- bump it locally for that run.
local WIN, CAP = 60, 4500

local function r16(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function r32(a) return emu.read32(a, emu.memType.snesWorkRam) end

local frames = 0
local lastops, lastflips = 0, 0
local peak, low = 0, 0
local booted = false

emu.addEventCallback(function()
  frames = frames + 1
  if frames % WIN ~= 0 then return end

  local st = r16(STATUS)
  if st == 0xDEAD then print("BENCH PANIC f=" .. frames) emu.stop(1) return end
  if not booted then
    if r16(MAGIC) ~= 0xCAFE then
      if frames >= 600 then print("BENCH NO-BOOT") emu.stop(1) end
      return
    end
    booted = true
  end

  local ops, flips = r32(VMOPS), r16(FLIPS)
  local dops, dflips = ops - lastops, flips - lastflips
  print(string.format("B f=%d ops=%d dops=%d flips=%d dflips=%d",
                      frames, ops, dops, flips, dflips))
  lastops, lastflips = ops, flips
  if dops > peak then peak = dops end

  -- plateau: the compute phase ended (VM benches); flip/draw benches never
  -- trip this (dops stays tiny, peak stays under the activity threshold)
  if peak >= 300 and dops < peak / 20 then
    low = low + 1
  else
    low = 0
  end
  local done = st == 1 or low >= 2 or frames >= CAP
  if done then
    print(string.format("BENCH SUMMARY peak_ops_sec=%d frames=%d ops=%d flips=%d",
                        peak, frames, ops, flips))
    print("BENCH done")
    emu.stop(0)
  end
end, emu.eventType.endFrame)
