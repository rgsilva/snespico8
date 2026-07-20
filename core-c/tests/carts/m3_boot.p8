pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- M3 boot/measurement cart: exercises the shared pixel pipeline
-- (rectfill, pset, circ) with a modest, bounded op count so the -O0
-- 65816 interpreter finishes within Mesen's script time limit.
function _init()
 cls(1)
 rectfill(0,0,63,63,8)
 rectfill(64,0,127,63,11)
 rectfill(0,64,63,127,3)
 rectfill(64,64,127,127,14)
 for i=0,127 do
  pset(i,i,7)
  pset(127-i,i,10)
 end
 circ(64,64,30,9)
end

function _update() end
function _draw() end
