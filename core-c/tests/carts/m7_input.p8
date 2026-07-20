pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m7_input: live-controller probe. Moves a dot with the d-pad and recolors
-- it with O/X, then exposes x,y,color at 0x4300-0x4302 so the Mesen harness
-- (snes/test/input.lua) can assert the real SNES pad reaches btn() end to
-- end -- the first genuinely interactive build (no baked input trace).
x=64 y=64 c=8
function _update()
 if btn(0) then x-=1 end
 if btn(1) then x+=1 end
 if btn(2) then y-=1 end
 if btn(3) then y+=1 end
 if btn(4) then c=11 end
 if btn(5) then c=12 end
 if x<0 then x=0 end
 if x>127 then x=127 end
 if y<0 then y=0 end
 if y>127 then y=127 end
 poke(0x4300,x)
 poke(0x4301,y)
 poke(0x4302,c)
end
function _draw()
 cls(1)
 rectfill(x-2,y-2,x+2,y+2,c)
 print("input",46,2,7)
 print(x.." "..y,44,116,6)
end
