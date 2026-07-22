pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m9 t7 inc4: all()-iteration probe -- tforcall/tforloop + the C
-- iterator step dominate, exactly the 17%-of-celeste path the
-- direct-table all_iter rewrite targets. all in _init so the live
-- op-counter slope is render-free.
function _init()
 printh("bench_all start")
 local t={}
 for i=1,32 do t[i]=i end
 local s=0
 for n=1,250 do
  for v in all(t) do
   s = s + v
  end
 end
 g=s
 printh("bench_all done")
end
function _update() end
function _draw() end
