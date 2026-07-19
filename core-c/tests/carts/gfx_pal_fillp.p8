pico-8 cartridge
version 42
__lua__
-- draw palette, transparency, fill patterns
function _init()
 cls(0)
 rectfill(0,0,30,30,8)
 pal(8,11)
 rectfill(32,0,62,30,8)  -- draws as 11
 pal()
 fillp(0b0011001111001100)
 rectfill(0,40,30,70,0x9e) -- two-color pattern
 fillp(0b0011001111001100.1)
 rectfill(32,40,62,70,0x9e) -- secondary transparent
 fillp()
 rectfill(64,40,94,70,9)
 for y=0,7 do for x=0,7 do sset(x,120+y,(x+y)%2==0 and 3 or 0) end end
 fillp(0b1010010110100101)
 circfill(100,20,12,0x7d)
 fillp()
 pal(3,-8) -- negative color index masks to 8
 rectfill(96,40,120,70,3)
 pal()
end
function _draw() end
