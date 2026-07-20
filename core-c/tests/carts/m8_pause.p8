pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m8_pause: registers two pause-menu items whose callbacks poke a marker to
-- 0x4300, so the Mesen harness (snes/test/pause.lua) can drive Start -> menu
-- -> select and verify the callback ran. Also moves a dot with the pad.
poke(0x4300,0)
menuitem(1,"say hi",function() poke(0x4300,11) end)
menuitem(2,"say bye",function() poke(0x4300,22) end)
x=64
function _update()
 if btn(1) then x+=1 end
 if btn(0) then x-=1 end
 if x<0 then x=0 end
 if x>127 then x=127 end
end
function _draw()
 cls(1)
 print("pause test",44,8,7)
 print("start = menu",38,58,6)
 print("fired: "..peek(0x4300),40,74,10)
 rectfill(x-2,98,x+2,102,8)
end
