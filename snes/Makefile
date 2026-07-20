# snespico8 SNES-side build. M0 spikes for now.
# Calypsi C for the runtime core (see docs/PROJECT.md), WLA-DX for probe
# ROMs and later asm glue.

CALDIR := ../tools/calypsi-65816-5.18
CC := $(CALDIR)/bin/cc65816
AS := $(CALDIR)/bin/as65816
LN := $(CALDIR)/bin/ln65816

# The full runtime (M3+) builds with 5.17 at -O0 — the correctness
# baseline established in M0b (the -O2 sequencing miscompiles are avoided
# at -O0; see bugs/). The M0 spikes above still use 5.18/-O2.
BUILD := build
P8CALDIR := ../tools/calypsi-65816-5.17
P8CC := $(P8CALDIR)/bin/cc65816
P8AS := $(P8CALDIR)/bin/as65816
P8LN := $(P8CALDIR)/bin/ln65816
CORE := ../core-c/src
P8CFLAGS := --core=65816 --target=snes --code-model=large --data-model=large \
            --no-ppu-mul -O0 -I $(CORE) -I .
LUAOBJS := lapi lauxlib lbaselib lcode lctype lcorolib ldebug ldo ldump \
           lfunc lgc llex lmem lobject lopcodes lparser lstate lstring \
           ltable ltm lundump lvm lzio lpico8lib
COREOBJS := p8num p8 p8gfx
P8LUA_O := $(addprefix $(BUILD)/p8obj/lua_,$(addsuffix .o,$(LUAOBJS)))
P8CORE_O := $(addprefix $(BUILD)/p8obj/,$(addsuffix .o,$(COREOBJS)))

PVBIN := ../../pvsneslib/devkitsnes/bin
WLA := $(PVBIN)/wla-65816
WLALINK := $(PVBIN)/wlalink

MESEN := /Applications/Mesen.app/Contents/MacOS/Mesen

MODEL := --code-model=large --data-model=large
OPT ?= -O2
CFLAGS = --core=65816 --target=snes $(MODEL) --no-ppu-mul $(OPT) --list-file=$(@:.o=.lst)
AFLAGS := --core=65816 --target=SNES $(MODEL)
LNFLAGS := calypsi/linker.scm --output-format=raw --rtattr exit=simplified \
           $(CALDIR)/lib/clib-lc-ld-snes-noppu.a

BUILD := build

.PHONY: all hello test-hello m7demo test-m7demo clean
all: hello m7demo
hello: $(BUILD)/hello.sfc
m7demo: $(BUILD)/m7demo.sfc

$(BUILD)/m7tables.inc: tools/mkm7.py | $(BUILD)
	python3 tools/mkm7.py $@

$(BUILD)/m7demo.o: m7demo.asm $(BUILD)/m7tables.inc | $(BUILD)
	$(WLA) -o $@ $<

$(BUILD)/m7demo.sfc: $(BUILD)/m7demo.o
	printf '[objects]\n$(BUILD)/m7demo.o\n' > $(BUILD)/link_m7
	$(WLALINK) -d -s -c $(BUILD)/link_m7 $@

test-m7demo: $(BUILD)/m7demo.sfc
	sh test/run_m7.sh

# ---- M3: the runtime core booting an embedded cart ------------------
.PHONY: p8 test-p8
p8: $(BUILD)/p8.sfc

CART ?= ../core-c/tests/carts/m3_boot.p8

# cart_hello.h always regenerates (FORCE), but cart2c.py is write-if-changed,
# so its mtime only bumps when CART actually changes — switching carts
# rebuilds p8main.o without relying on (1-second-granular) mtimes.
# FRAMES = frames the main loop runs (M4 parity keeps it small so the
# extra per-flip hashing fits Mesen's script time limit). INPUT = optional
# sidecar file of per-frame hex button masks.
FRAMES ?= 8
.PHONY: FORCE
$(BUILD)/cart_hello.h: $(CART) tools/cart2c.py FORCE | $(BUILD)
	python3 tools/cart2c.py $(CART) $@ $(FRAMES) $(INPUT)

$(BUILD)/p8obj:
	mkdir -p $(BUILD)/p8obj

$(BUILD)/p8obj/lua_%.o: $(CORE)/lua/%.c | $(BUILD)/p8obj
	$(P8CC) -o $@ $< $(P8CFLAGS)

$(BUILD)/p8obj/%.o: $(CORE)/%.c | $(BUILD)/p8obj
	$(P8CC) -o $@ $< $(P8CFLAGS)

# P8MAINDEF: parity builds pass -DP8_PARITY to enable per-flip RAM hashes.
# The default (demo/boot) ROM omits them so it fits the script time limit.
P8MAINDEF ?=
$(BUILD)/p8obj/p8main.o: p8main.c $(BUILD)/cart_hello.h | $(BUILD)/p8obj
	$(P8CC) -o $@ p8main.c $(P8CFLAGS) -I $(BUILD) $(P8MAINDEF)

$(BUILD)/p8obj/p8heap.o: p8heap.c p8heap.h | $(BUILD)/p8obj
	$(P8CC) -o $@ p8heap.c $(P8CFLAGS)

$(BUILD)/p8obj/stubs.o: stubs.c calypsi/mailbox.h | $(BUILD)/p8obj
	$(P8CC) -o $@ stubs.c $(P8CFLAGS)

$(BUILD)/p8obj/mailbox.o: calypsi/mailbox.c calypsi/mailbox.h | $(BUILD)/p8obj
	$(P8CC) -o $@ calypsi/mailbox.c $(P8CFLAGS)

$(BUILD)/p8obj/header.o: calypsi/header.s | $(BUILD)/p8obj
	$(P8AS) -o $@ $< --core=65816 --target=SNES --code-model=large --data-model=large

$(BUILD)/p8.raw: $(P8LUA_O) $(P8CORE_O) $(BUILD)/p8obj/p8main.o \
                 $(BUILD)/p8obj/p8heap.o $(BUILD)/p8obj/stubs.o \
                 $(BUILD)/p8obj/mailbox.o $(BUILD)/p8obj/header.o
	$(P8LN) -o $@ calypsi/linker.scm --output-format=raw --rtattr exit=simplified \
	  $(P8CALDIR)/lib/clib-lc-ld-snes-noppu.a --list-file=$(BUILD)/p8.map $^

$(BUILD)/p8.sfc: $(BUILD)/p8.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-p8: $(BUILD)/p8.sfc
	$(MESEN) --testrunner $(BUILD)/p8.sfc test/p8boot.lua

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/header.o: calypsi/header.s | $(BUILD)
	$(AS) -o $@ $< $(AFLAGS)

$(BUILD)/mailbox.o: calypsi/mailbox.c calypsi/mailbox.h | $(BUILD)
	$(CC) -o $@ $< $(CFLAGS)

$(BUILD)/hello_main.o: hello.c calypsi/mailbox.h | $(BUILD)
	$(CC) -o $@ $< $(CFLAGS)

$(BUILD)/hello.raw: $(BUILD)/header.o $(BUILD)/mailbox.o $(BUILD)/hello_main.o
	$(LN) -o $@ $(LNFLAGS) --list-file=$(BUILD)/hello.map $^

$(BUILD)/hello.sfc: $(BUILD)/hello.raw tools/raw2sfc.py
	python3 tools/raw2sfc.py $< $@

test-hello: $(BUILD)/hello.sfc
	$(MESEN) --testrunner $(BUILD)/hello.sfc test/hello.lua

clean:
	rm -rf $(BUILD)
