pico-8 cartridge // http://www.pico-8.com
version 42
__lua__
-- m9 t0: flip cost. empty update/draw means every game frame is only the
-- implicit flip, so wall-frames / flips == frames per flip (the t4 metric).
function _init()
 printh("bench_flip start")
end
function _update() end
function _draw() end
