pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m9 t0: draw-bound / api-heavy, near-vm-free: the t3+t4 fps metric.
-- fills, lines, circles and text every frame -- per-pixel core work.
function _init()
 printh("bench_draw start")
end
function _update() end
function _draw()
 cls(1)
 rectfill(0,64,127,127,2)
 for i=0,7 do
  line(0,i*8,127,i*8+4,8+i)
 end
 for i=0,7 do
  circfill(i*16+8,96,7,i)
 end
 print("bench draw",2,2,7)
end
