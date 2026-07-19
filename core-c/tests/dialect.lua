-- PICO-8 dialect statement vectors: run identically by runp8 (as a
-- cart, stdout via print) and by the z8lua oracle. Keep to features
-- both engines implement (no peek/poke here: z8lua has no RAM).
local a = 5
a += 3 print(a)
a -= 1 print(a)
a *= 2 print(a)
a /= 7 print(a)
a \= 1 print(a)
a %= 3 print(a)
a ^= 2 print(a)
local s = "ab"
s ..= "cd" print(s)
print(7 \ 2)
print(-7 \ 2)
print(7.9 \ 1)
print(5 ^^ 3)
print(0xff ^^ 0x0f)
print(1 <<> 4)
print(-32768 >>< 4)
print(-32768 >>> 1)
print(-32768 >> 1)
print(1 << 4)
print(256 >> 4)
print(1 << -2)
print(0x0.0001 <<> 16)
if (a > 0) print("pos")
if (a > 99999) print("nope")
if (1 != 2) print("neq")
local i = 0
for k=1,3 do i += k end
print(i)
? "qmark"
? 42
