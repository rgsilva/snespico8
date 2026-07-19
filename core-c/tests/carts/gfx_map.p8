pico-8 cartridge
version 42
__lua__
-- map: cells, layers, shared region, mget/mset
function _init()
 cls(1)
 for y=0,7 do for x=0,7 do
  sset(8+x,y,8) sset(16+x,y,11) sset(24+x,y,10)
 end end
 fset(1,0,true) fset(2,1,true) fset(3,0,true) fset(3,1,true)
 mset(0,0,1) mset(1,0,2) mset(2,0,3) mset(3,0,0)
 mset(0,40,2) -- shared region row
 map(0,0,10,10,4,1)        -- all
 map(0,0,10,20,4,1,1)      -- layer 1: sprites with flag 0
 map(0,0,10,30,4,1,2)      -- layer 2: flag 1
 map(0,40,10,40,1,1)       -- shared-region cel
 printh(mget(1,0)) printh(mget(0,40)) printh(mget(99,99))
 printh(fget(3)) printh(tostr(fget(2,1)))
end
function _draw() end
