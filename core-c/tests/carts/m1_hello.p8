pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m1 hello: 16 color stripes + white diagonal, the m7demo pattern.
-- plain lua 5.2 syntax only (dialect lands in m1c).
function _init()
 cls(0)
 for tx=0,15 do
  for x=tx*8,tx*8+7 do
   for y=0,127 do
    pset(x,y,tx)
   end
  end
 end
 for i=0,127 do
  pset(i,i,7)
 end
end

function _update()
end

function _draw()
end
