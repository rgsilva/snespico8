pico-8 cartridge
version 42
__lua__
-- print: positioning, colors, newline, wide glyphs, cursor+scroll
function _init()
 cls(0)
 local nx = print("hello world",2,2,7)
 print(nx,60,2,8)
 print("line1\nline2",2,10,11)
 print("\136\137\142\145",2,24,10) -- wide glyphs (>=128)
 print("mixed\145case",2,34,12)
 cursor(80,40,14)
 print("cur1")
 print("cur2")
 print(123.5,2,44)
 print(true,30,44,9)
end
function _draw() end
