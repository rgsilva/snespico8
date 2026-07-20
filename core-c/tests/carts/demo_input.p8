pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- demo_input: live controller readout. Press buttons and watch them light
-- up -- a visible proof that the real SNES pad reaches btn(). D-pad on the
-- left, O/X on the right (O = SNES B or Y, X = SNES A or X). A snespico8
-- demo-ROM test cart. Hold Select to return to the menu.
function _draw()
 cls(1)
 print("joypad test",40,6,7)
 -- d-pad cross (u/d/l/r) + face buttons (o/x)
 key(40,60,2,"u")   -- up
 key(40,80,3,"d")   -- down
 key(28,70,0,"l")   -- left
 key(52,70,1,"r")   -- right
 key(92,78,4,"o")   -- o
 key(108,66,5,"x")  -- x
 print("o=b/y  x=a/x",34,104,13)
 print("press to light up",26,116,6)
end
function key(x,y,b,ch)
 local on=btn(b)
 rectfill(x-5,y-5,x+5,y+5,on and 10 or 5)
 print(ch,x-1,y-2,on and 0 or 7)
end
