pico-8 cartridge
version 42
__lua__
-- sprites: sheet via sset, flips, fractional size, sspr scaling
function _init()
 cls(0)
 -- sprite 1: asymmetric arrow; sprite 2: gradient
 for y=0,7 do for x=0,7 do
  sset(8+x,y,(x==y or y==3) and 8 or (x<2 and 12 or 0))
  sset(16+x,y,flr(x/2)+1)
 end end
 spr(1,10,10)
 spr(1,30,10,1,1,true,false)
 spr(1,50,10,1,1,false,true)
 spr(1,70,10,1,1,true,true)
 spr(1,90,10,0.5,1) -- fractional width
 spr(2,10,30,2,1)   -- 2 tiles wide (sprite 2+3)
 sspr(8,0,8,8,10,50,32,16) -- scale up
 sspr(8,0,8,8,60,50,4,4)   -- scale down
 sspr(8,0,8,8,80,50,16,16,true,true)
 palt(0,false) palt(12,true)
 spr(1,110,10)
 palt()
end
function _draw() end
