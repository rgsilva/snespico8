pico-8 cartridge
version 42
__lua__
-- lines: octants, continuation state, camera+clip
function _init()
 cls(0)
 for i=0,7 do
  line(64,64,64+flr(30*cos(i/8)),64+flr(30*sin(i/8)),7+i%8)
 end
 line(0,0,20,0,8)
 line(20,10) -- continuation from (20,0)
 line(0,10)
 line()
 line(5,120,9) -- first point after invalidation
 clip(90,90,20,20)
 line(80,80,127,127,10)
 clip()
 camera(-10,-100)
 line(0,0,20,5,11)
 camera()
end
function _draw() end
