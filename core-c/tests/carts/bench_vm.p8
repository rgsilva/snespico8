pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m9 t0: pure-vm throughput probe. phase 1 is the m3-continuity add loop
-- (same shape as m3_bench, sized up); phase 2 is mul-heavy for the t5
-- metric. both run in _init so the live op-counter slope is render-free.
function _init()
 printh("bench_vm start")
 local s=0
 for i=1,24000 do s=s+i end
 local m=1
 for i=1,4000 do m=m*3+i end
 g=s+m
 printh("bench_vm done")
end
function _update() end
function _draw() end
