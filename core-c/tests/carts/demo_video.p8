pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- demo_video: exercises the Mode 7 framebuffer path -- the 16-colour palette
-- as bars, an animated ring of pulsing circles, and a frame counter. Pure
-- graphics, no input or audio. One of the snespico8 demo-ROM test carts.
t=0
function _update()
 t+=1
end
function _draw()
 cls(0)
 -- the 16-colour palette as bars across the top
 for c=0,15 do
  rectfill(c*8,0,c*8+7,30,c)
 end
 -- an animated ring of pulsing circles
 for i=0,7 do
  local a=t/128+i/8
  local x=64+cos(a)*34
  local y=74+sin(a)*34
  circfill(x,y,4+(t+i*5)%9,8+i)
 end
 print("video test",44,116,7)
 print("frame "..t,2,36,6)
end
