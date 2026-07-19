pico-8 cartridge
version 42
__lua__
-- rect/circ/oval outlines and fills
function _init()
 cls(1)
 rect(2,2,30,20,7)
 rectfill(4,4,28,18,8)
 rect(40,20,34,4,10) -- swapped corners
 circ(80,16,10,11)
 circfill(110,16,8,12)
 circ(80,16,0,7)  -- r=0 single dot
 circfill(90,40,3,9)
 oval(2,40,40,60,14)
 ovalfill(50,44,80,56,15)
 oval(100,40,100,60,6) -- degenerate vertical
 rectfill(0,100,127,127,2)
end
function _draw() end
