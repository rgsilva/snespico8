# snespico8 PC reference core. The same src/ tree must stay compilable
# by Calypsi for the SNES build (M3) — C99, no int64, no host-isms.
CC ?= cc
CFLAGS = -O2 -std=c99 -Wall -Wextra -Wno-unused-parameter -Isrc

LUA = lapi lauxlib lbaselib lcode lctype lcorolib ldebug ldo ldump lfunc lpico8lib \
      lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltm \
      lundump lvm lzio

BUILD = build
LUA_OBJS = $(addprefix $(BUILD)/lua_,$(addsuffix .o,$(LUA)))

all: $(BUILD)/runp8

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/lua_%.o: src/lua/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/p8num.o: src/p8num.c src/p8num.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/p8.o: src/p8.c src/p8.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/runp8.o: src/runp8.c src/p8.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/runp8: $(BUILD)/runp8.o $(BUILD)/p8.o $(BUILD)/p8num.o $(LUA_OBJS)
	$(CC) -o $@ $^ -lm

test: $(BUILD)/runp8
	$(BUILD)/runp8 tests/carts/m1_hello.p8 -n 3 -p $(BUILD)/m1_hello.ppm

clean:
	rm -rf $(BUILD)

.PHONY: all test clean
