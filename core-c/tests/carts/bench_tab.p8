pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m9 t6: gameplay-shaped vm probe -- table reads, tests and adds in the
-- proportions the celeste opcode histogram shows (gettable/test/eq
-- dominant). all in _init so the live op-counter slope is render-free.
function _init()
 printh("bench_tab start")
 local t={}
 for i=1,32 do t[i]=i end
 local s=0
 for n=1,250 do
  for i=1,32 do
   if t[i] > 16 then s = s + t[i] end
   if t[i] == 7 then s = s + 1 end
  end
 end
 g=s
 printh("bench_tab done")
end
function _update() end
function _draw() end
