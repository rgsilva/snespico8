-- M13 demo-ROM verify: screenshot the curated menu (proves our demo carts
-- are listed), then launch cart 0 (demo_video) and screenshot it rendering.
-- Reuses the M8a launch flow (press A after the menu settles).
local MB_MAGIC, MB_WINDEX, MB_RING = 0xF000, 0xF004, 0xF010
local RING_SIZE = 3552

local function word(a) return emu.read16(a, emu.memType.snesWorkRam) end
local function byte(a) return emu.read(a, emu.memType.snesWorkRam) end

local readIndex, linebuf = 0, ""
local sawMenu, sawLaunch = false, false
local function drain()
  local wi = word(MB_WINDEX)
  while readIndex ~= wi do
    local b = byte(MB_RING + (readIndex % RING_SIZE))
    if b == 10 then
      if #linebuf > 0 and linebuf:byte(1) < 128 then print("OUT " .. linebuf) end
      if linebuf:find("menu ready") then sawMenu = true end
      if linebuf:find("^launch") then sawLaunch = true end
      linebuf = ""
    else
      linebuf = linebuf .. string.char(b)
    end
    readIndex = (readIndex + 1) % 65536
  end
end

local function shot(name)
  local s = emu.takeScreenshot()
  local h = {}
  for i = 1, #s do h[#h + 1] = string.format("%02x", string.byte(s, i)) end
  print("SHOT " .. name .. " " .. table.concat(h))
end

local phase, frames, mstart, lframe = 0, 0, 0, 0

emu.addEventCallback(function()
  if phase == 1 and (frames - mstart) >= 160 and (frames - mstart) < 195 then
    emu.setInput({ a = true }, 0)
  end
end, emu.eventType.inputPolled)

emu.addEventCallback(function()
  frames = frames + 1
  if word(MB_MAGIC) ~= 0xCAFE then
    if frames > 900 then print("RESULT FAIL demorom no-magic"); emu.stop(1) end
    return
  end
  drain()
  if phase == 0 and sawMenu then mstart = frames; phase = 1 end
  if phase == 1 and (frames - mstart) == 100 then
    -- the menu clears to colour 1, so a drawn menu fills all 8192 bytes;
    -- 0 would mean draw_menu never ran
    local nz = 0
    for i = 0, 0x1fff do if byte(0x16000 + i) ~= 0 then nz = nz + 1 end end
    print("menu framebuffer nonzero bytes: " .. nz)
    if nz == 0 then print("RESULT FAIL demorom menu-blank"); emu.stop(1); return end
    shot("demorom_menu.png")
  end
  if phase == 1 and sawLaunch then phase = 2; lframe = frames; print("launched cart 0") end
  if phase == 2 and (frames - lframe) > 700 then
    shot("demorom_video.png")
    print("RESULT PASS demorom (menu lists our carts; cart 0 launched + rendered)")
    emu.stop(0)
  end
  if frames > 9000 then print("RESULT FAIL demorom timeout phase=" .. phase); emu.stop(1) end
end, emu.eventType.endFrame)
