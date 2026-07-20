pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- M4 input parity: a rect driven by the d-pad, so btn() and the framebuffer
-- change every frame — proves input handling + dynamic pixel parity.
x=60 y=60
function _update()
 if btn(0) then x-=3 end
 if btn(1) then x+=3 end
 if btn(2) then y-=3 end
 if btn(3) then y+=3 end
end
function _draw()
 cls(1)
 rectfill(x,y,x+12,y+12,8)
 pset(x,y,7)
 circ(x+6,y+6,4,11)
end
