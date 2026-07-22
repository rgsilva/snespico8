# snespico8 PC reference core. The same src/ tree must stay compilable
# by Calypsi for the SNES build (M3) — C99, no int64, no host-isms.
CC ?= cc
# -fwrapv: the core relies on 2's-complement wraparound for its fixed-point
# math (which the 65816 gives in hardware, and Calypsi/clang happen to emit);
# without it gcc's strict signed-overflow UB diverges (e.g. sin(0.25) came out
# -1 on gcc-Linux vs -1.1055 everywhere else). Forcing wraparound makes the
# host match the SNES semantics the code is written for.
# P8_OPSTATS: host-only per-opcode counters behind runp8 -S (T6 evidence
# tooling); one array increment per dispatched op, never in the SNES build.
CFLAGS = -O2 -std=c99 -fwrapv -Wall -Wextra -Wno-unused-parameter -Isrc -DP8_OPSTATS

LUA = lapi lauxlib lbaselib lcode lctype lcorolib ldebug ldo ldump lfunc lpico8lib \
      lgc llex lmem lobject lopcodes lparser lstate lstring ltable ltable_get ltm \
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

$(BUILD)/p8gfx.o: src/p8gfx.c src/p8.h src/p8int.h src/p8font.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/p8.o: src/p8.c src/p8.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/p8audio.o: src/p8audio.c src/p8.h src/p8int.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/runp8.o: src/runp8.c src/p8.h | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# p8heap.c is the SNES region allocator; compiled into runp8 only for the
# -M heap-measurement mode (M5). It stays portable C (no SNES-isms).
$(BUILD)/p8heap.o: ../snes/p8heap.c ../snes/p8heap.h | $(BUILD)
	$(CC) $(CFLAGS) -I../snes -c -o $@ $<

$(BUILD)/runp8: $(BUILD)/runp8.o $(BUILD)/p8.o $(BUILD)/p8gfx.o $(BUILD)/p8audio.o $(BUILD)/p8num.o $(BUILD)/p8heap.o $(LUA_OBJS)
	$(CC) -o $@ $^ -lm

test: $(BUILD)/runp8
	$(BUILD)/runp8 tests/carts/m1_hello.p8 -n 3 -p $(BUILD)/m1_hello.ppm

# Full host suite, run locally on the same platform the SNES build is
# validated against (macOS here). Diffs the language vectors against the
# z8lua oracle (../tools/z8lua/z8lua) and the gfx/audio goldens. Excludes
# suites needing gitignored bits: celeste (BBS cart), realvec (real PICO-8),
# snesparity (Calypsi + Mesen).
check: $(BUILD)/runp8
	bash tests/numvec.sh
	bash tests/dialectvec.sh
	bash tests/libvec.sh
	bash tests/gfxvec.sh
	bash tests/audiovec.sh

# The subset that is deterministic across host compilers -- what CI runs.
# libvec is left out: it probes sin/cos at their extremes, where z8lua
# overshoots to +/-1.1055 (a fixed-point quirk our core reproduces on clang
# and on the 65816, confirmed bit-exact by M4 parity); gcc rounds it to
# +/-1, so libvec is host-compiler sensitive. It stays a local/reference
# check, not a CI gate.
check-ci: $(BUILD)/runp8
	bash tests/numvec.sh
	bash tests/dialectvec.sh
	bash tests/gfxvec.sh
	bash tests/audiovec.sh

# M9: differential fuzzer gating every p8num rewrite (exhaustive toint,
# pow2-divisor div sweep, luaconf macro replicas). Single TU on purpose:
# compiling p8num.c into the same object lets the host compiler inline
# both sides, so the 2^32 sweep takes seconds. Run it via taskpolicy or
# a foreground shell -- macOS background QoS can throttle it 5-10x.
fuzz: | $(BUILD)
	$(CC) -O3 -std=c99 -fwrapv -Wall -Wextra -o $(BUILD)/fixfuzz tests/fixfuzz.c src/p8num.c -Isrc
	$(BUILD)/fixfuzz

clean:
	rm -rf $(BUILD)

.PHONY: all test check check-ci fuzz clean
