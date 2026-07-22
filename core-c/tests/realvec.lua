-- Real-PICO-8 oracle vectors: this body runs identically under
-- `pico8 -x` (printh -> stdout) and runp8 (printh -> stderr).
-- Gfx stages hash the framebuffer themselves via @ peeks, so no
-- screenshot machinery is needed — the hash IS the comparison.

-- 32-bit FNV-ish hash over the screen, all raw-bit ops (exact in fix32)
function shash(lbl)
 local h = 0x811c.9dc5
 for a = 0x6000, 0x7fff do
  h = bxor(h, @a)
  h = bxor(shl(h, 5), lshr(h, 27), h) -- cheap avalanche, bit-exact
 end
 printh(lbl.." "..tostr(h, 1))
end

-- numeric / semantic spot checks
printh("mod0 "..tostr(5 % 0).." "..tostr(-5 % 0))
printh("idiv0 "..tostr(7 \ 0).." "..tostr(-7 \ 0))
printh("pow "..tostr((-2) ^ 0.5).." "..tostr(2 ^ -0.5))
printh("div0 "..tostr(1 / 0).." "..tostr(-1 / 0))
printh("wrap "..tostr(0x7fff.ffff + 0x0.0001))
printh("fmt "..tostr(-0.0001, 1).." "..tostr(0.1 + 0.2).." "..tostr(1000000 / 3))
printh("parse "..tostr(0.1, 1).." "..tostr(0.33, 1).." "..tostr(0.12345678, 1)
       .." "..tostr(0.9999999, 1).." "..tostr(0.00008, 1))
printh("trig "..tostr(sin(0.125), 1).." "..tostr(cos(0.33), 1)
       .." "..tostr(atan2(0.5, 0.7), 1))
printh("sqrt "..tostr(sqrt(2), 1).." "..tostr(sqrt(0.5), 1))

-- seeded rnd streams (draws only; raw state is a known divergence)
srand(42)
printh("rnd42 "..tostr(rnd(), 1).." "..tostr(rnd(), 1).." "..tostr(rnd(10), 1))
srand(0)
printh("rnd0 "..tostr(rnd(), 1).." "..tostr(rnd(), 1))
srand(0x1234.5678)
printh("rndfx "..tostr(rnd(100), 1).." "..tostr(flr(rnd(8))))

-- gfx: shapes through the shared pixel pipeline
cls(0)
shash("cls0")
cls(5)
rectfill(10, 10, 60, 40, 8)
rect(5, 5, 100, 100, 7)
line(0, 127, 127, 0, 11)
line(64, 0, 64, 127, 3)
shash("shapes")
circ(64, 64, 20, 10)
circfill(30, 90, 12, 9)
oval(70, 80, 120, 110, 12)
ovalfill(80, 20, 110, 45, 14)
shash("circs")

-- camera + clip
cls(1)
camera(-10, -20)
clip(15, 25, 60, 50)
rectfill(0, 0, 127, 127, 13)
pset(0, 0, 7)
camera()
clip()
shash("camclip")

-- self-made sprites: sset then spr/sspr (no __gfx__ section needed)
cls(0)
for y = 0, 7 do
 for x = 0, 7 do
  sset(x, y, (x + y) % 16)
 end
end
for y = 0, 7 do
 for x = 0, 7 do
  sset(8 + x, y, (x * y) % 16)
 end
end
spr(0, 10, 10)
spr(1, 30, 10)
spr(0, 50, 10, 1, 1, true, false)
spr(0, 70, 10, 1, 1, false, true)
spr(0, 90, 10, 2, 1)
sspr(0, 0, 8, 8, 10, 40, 32, 32)
sspr(0, 0, 8, 8, 60, 40, 16, 24, true, true)
shash("sprites")

-- map: mset then draw
for i = 0, 15 do
 mset(i, 0, i % 2)
 mset(i, 1, 1 - i % 2)
end
cls(0)
map(0, 0, 4, 4, 16, 2)
shash("map")
printh("mget "..tostr(mget(3, 0)).." "..tostr(mget(3, 1)))

-- pal / palt / fillp
cls(0)
pal(8, 11)
rectfill(0, 0, 20, 20, 8)
palt(0, false)
palt(11, true)
spr(0, 40, 0)
pal()
fillp(0b0101101001011010)
rectfill(60, 0, 100, 30, 0x4e)
fillp(0b0101101001011010.1)
rectfill(60, 40, 100, 70, 0x4e)
fillp()
circfill(30, 60, 10, 8)
shash("palfillp")

-- print / font
cls(0)
print("hello world", 4, 4, 7)
print("HELLO", 4, 14, 8)
print("0123456789", 4, 24, 9)
print("!@#$%^&*()[]{}<>", 4, 34, 10)
local nx = print("x:", 4, 44, 11)
print(tostr(nx), 30, 44, 12)
print("multi\nline", 4, 54, 13)
shash("print")

-- pget semantics
cls(2)
pset(10, 10, 9)
camera(5, 5)
printh("pget "..tostr(pget(10, 10)).." "..tostr(pget(15, 15)))
camera()

-- btnp defaults (no input held: just exercise the call)
printh("btn "..tostr(btn(0)).." "..tostr(btnp(4)))

-- str[pos] sugar (0.2.5): sub(str,pos,pos) semantics exactly -- floor,
-- negative counts from the end, out of range is "" (never nil).
-- M9 T7 pins this: the first port indexed with raw fix32 bits, so
-- every normal index returned nil and no vector noticed.
local sv = "snes"
printh("stridx "..sv[1]..sv[4]..tostr(sv[-1])..tostr(sv[-4]))
printh("stridx2 "..tostr(sv[0]).."|"..tostr(sv[5]).."|"..tostr(sv[-5]))
printh("stridx3 "..tostr(sv[1.5])..tostr(sv[-0.5]))
for k=-6,6 do
  if sv[k] ~= sub(sv,k,k) then printh("stridx4 DIVERGES at "..k) end
end
printh("stridx4 s[k]==sub(s,k,k) all k")

-- all()/foreach step semantics, incl. mutation during iteration
-- (M9 T7 pins the direct-table iterator rewrite against the console)
local it = {1,2,3,4,5}
local acc = ""
for v in all(it) do acc = acc..tostr(v) end
printh("all1 "..acc)
acc = ""
for v in all(it) do
 if v == 2 then del(it, 2) end -- delete the current element mid-loop
 acc = acc..tostr(v)
end
printh("all2 "..acc.." n="..tostr(#it))
it = {1,2,3}
acc = ""
foreach(it, function(v)
 acc = acc..tostr(v)
 if v == 1 then del(it, 1) end
end)
printh("fe1 "..acc.." n="..tostr(#it))
it = {}
for v in all(it) do acc = "never" end
printh("all3 empty ok")
it = {1,2,3}
it[2] = nil -- hole: all() skips nils but still reaches 3
acc = ""
for v in all(it) do acc = acc..tostr(v) end
printh("all4 "..acc)

printh("realvec-end")
